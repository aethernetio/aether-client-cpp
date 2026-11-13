/*
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "aether/adaptive_datagram/rate_controller.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <vector>

namespace ae::adaptive_datagram {
namespace rate_controller_internal {

std::uint64_t SaturatingAdd(std::uint64_t first, std::uint64_t second) {
  return first > std::numeric_limits<std::uint64_t>::max() - second
             ? std::numeric_limits<std::uint64_t>::max()
             : first + second;
}

bool Elapsed(TimePoint now, TimePoint then, Duration interval) {
  // Convert the counts separately: signed now - then could overflow for the
  // full TimePoint range. Unsigned subtraction is exact for ordered timestamps.
  if (now < then) {
    return false;
  }
  auto const ticks =
      static_cast<std::uint64_t>(now.time_since_epoch().count()) -
      static_cast<std::uint64_t>(then.time_since_epoch().count());
  auto const interval_ticks =
      std::chrono::duration_cast<TimePoint::duration>(interval).count();
  return ticks >= static_cast<std::uint64_t>(interval_ticks);
}

}  // namespace rate_controller_internal

RateController::RateController(Config const& config, TimePoint now)
    : config_{config},
      valid_{config.Validate()},
      latest_time_{now},
      path_started_at_{now},
      last_control_at_{now},
      last_feedback_at_{now},
      last_rate_change_at_{now} {
  assert(valid_ && "Adaptive datagram configuration must be valid");
  metrics_.rate = valid_ ? config_.initial_rate_bytes_per_second : 0;
}

void RateController::RecordOutcome(TimePoint completed_at,
                                   std::size_t payload_bytes,
                                   bool delivered_on_time, Duration latency,
                                   std::optional<TimePoint> sent_at) {
  if (!valid_) {
    return;
  }
  latest_time_ = std::max(latest_time_, completed_at);
  metrics_.total_outcomes =
      rate_controller_internal::SaturatingAdd(metrics_.total_outcomes, 1);
  if (delivered_on_time) {
    metrics_.delivered_bytes = rate_controller_internal::SaturatingAdd(
        metrics_.delivered_bytes, payload_bytes);
  } else {
    metrics_.total_lost_packets =
        rate_controller_internal::SaturatingAdd(metrics_.total_lost_packets, 1);
  }

  TrimHistory(latest_time_);
  if (rate_controller_internal::Elapsed(latest_time_, completed_at,
                                        config_.loss_window)) {
    return;
  }
  // Completion normally arrives in chronological order. Inserting by time also
  // makes delayed reports deterministic without corrupting the sliding window.
  Outcome outcome{completed_at, payload_bytes, delivered_on_time, latency};
  if (outcomes_.empty() || outcomes_.back().completed_at <= completed_at) {
    outcomes_.push_back(outcome);
  } else {
    auto const position =
        std::upper_bound(outcomes_.begin(), outcomes_.end(), completed_at,
                         [](TimePoint time, Outcome const& item) {
                           return time < item.completed_at;
                         });
    outcomes_.insert(position, outcome);
  }
  if (outcomes_.size() > config_.max_history_packets) {
    auto const evicted_at = outcomes_.front().completed_at;
    if (!truncated_through_ || evicted_at > *truncated_through_) {
      truncated_through_ = evicted_at;
    }
    outcomes_.pop_front();
  }
  if (outcome_generation_ == std::numeric_limits<std::uint64_t>::max()) {
    // Preserve the new-sample distinction after counter saturation.
    outcome_generation_ = 1;
    evaluated_generation_ = 0;
  } else {
    ++outcome_generation_;
  }
  if (!delivered_on_time) {
    auto const origin = sent_at.value_or(completed_at);
    latest_loss_origin_ =
        latest_loss_origin_ ? std::max(*latest_loss_origin_, origin) : origin;
    if (loss_generation_ == std::numeric_limits<std::uint64_t>::max()) {
      loss_generation_ = 1;
      evaluated_loss_generation_ = 0;
    } else {
      ++loss_generation_;
    }
  }
  // Final outcomes mature after a feedback grace period. Their historical
  // latency belongs in the window but must not overwrite current path delay or
  // trigger a second fast reduction after the path has already recovered.
}

void RateController::RecordUnconfirmed(TimePoint completed_at) {
  if (!valid_) {
    return;
  }
  latest_time_ = std::max(latest_time_, completed_at);
  TrimHistory(latest_time_);
  if (rate_controller_internal::Elapsed(latest_time_, completed_at,
                                        config_.loss_window)) {
    return;
  }
  if (!latest_uncertain_at_ || completed_at > *latest_uncertain_at_) {
    latest_uncertain_at_ = completed_at;
  }
  metrics_.feedback_incomplete = true;
  metrics_.window_valid = false;
  healthy_intervals_ = 0;
  RefreshConstraints(last_queue_age_);
}

void RateController::ObserveLatency(Duration latency,
                                    std::optional<TimePoint> sent_at) {
  if (!valid_) {
    return;
  }
  latest_latency_ = latency;
  metrics_.latest_latency = latency;
  auto const previous = metrics_.smoothed_latency;
  metrics_.smoothed_latency =
      delay_samples_.empty()
          ? latency
          : Duration{static_cast<Duration::rep>(
                (std::uint64_t{previous.count()} * 7 + latency.count()) / 8)};
  auto const delay_window = std::max(config_.loss_window, Duration{3s});
  while (!delay_samples_.empty() &&
         rate_controller_internal::Elapsed(
             latest_time_, delay_samples_.front().first, delay_window)) {
    delay_samples_.pop_front();
  }
  delay_samples_.emplace_back(latest_time_, latency);
  if (delay_samples_.size() > config_.max_history_packets) {
    delay_samples_.pop_front();
  }
  // A median baseline rejects both low jitter outliers and short queue spikes.
  // Comparing an average to the absolute minimum mistakes ordinary jitter for
  // a permanently growing queue.
  std::vector<Duration> delays;
  delays.reserve(delay_samples_.size());
  for (auto const& sample : delay_samples_) {
    delays.push_back(sample.second);
  }
  auto middle = delays.begin() + static_cast<std::ptrdiff_t>(delays.size() / 2);
  std::nth_element(delays.begin(), middle, delays.end());
  metrics_.base_latency = *middle;
  auto const growing_queue = metrics_.smoothed_latency.count() >
                                 std::uint64_t{metrics_.base_latency.count()} +
                                     config_.latency_target.count() / 10 &&
                             metrics_.smoothed_latency > previous;
  pending_latency_violation_ =
      pending_latency_violation_ ||
      ((latency > config_.latency_target || growing_queue) &&
       (!sent_at || (*sent_at >= path_started_at_ &&
                     (!last_decrease_at_ || *sent_at > *last_decrease_at_))));
  RefreshConstraints(last_queue_age_);
}

void RateController::ObserveDeliveryRate(TimePoint now,
                                         std::uint64_t bytes_per_second) {
  if (!valid_ || bytes_per_second == 0 ||
      (delivery_rate_at_ && now <= *delivery_rate_at_)) {
    return;
  }
  metrics_.delivery_rate =
      std::min(bytes_per_second, config_.max_rate_bytes_per_second);
  delivery_rate_at_ = now;
}

void RateController::FeedbackReceived(TimePoint now, Duration rtt) {
  if (!valid_ || now < last_feedback_at_) {
    return;
  }
  latest_time_ = std::max(latest_time_, now);
  last_feedback_at_ = now;
  metrics_.rtt = rtt;
  metrics_.feedback_stale = false;
  stale_reduced_ = false;
  RefreshConstraints(last_queue_age_);
}

void RateController::Update(TimePoint now, Duration local_queue_age,
                            bool lower_blocked) {
  if (!valid_) {
    return;
  }
  // Supplied clock regressions cannot prematurely expire the window or allow
  // additional control steps. The stream must establish a new clock epoch by
  // ResetPath if its clock changes discontinuously.
  latest_time_ = std::max(latest_time_, now);
  now = latest_time_;
  last_queue_age_ = local_queue_age;
  RefreshWindow(now);
  metrics_.feedback_stale = rate_controller_internal::Elapsed(
      now, last_feedback_at_, config_.feedback_timeout);
  RefreshConstraints(local_queue_age);
  if (!rate_controller_internal::Elapsed(now, last_control_at_,
                                         config_.control_interval)) {
    return;
  }
  last_control_at_ = now;
  auto const new_outcomes = outcome_generation_ != evaluated_generation_;
  evaluated_generation_ = outcome_generation_;
  auto const new_losses = loss_generation_ != evaluated_loss_generation_;
  evaluated_loss_generation_ = loss_generation_;

  auto const queue_overdue =
      lower_blocked && local_queue_age > config_.latency_target;
  auto const queue_grew =
      queue_overdue && local_queue_age > last_reduced_queue_age_;
  if (!queue_overdue) {
    last_reduced_queue_age_ = Duration::zero();
  }
  auto const new_latency_violation = pending_latency_violation_;
  pending_latency_violation_ = false;
  auto const loss_over_target =
      metrics_.window_samples >= config_.min_samples &&
      window_lost_packets_ * 1000ULL >
          config_.max_loss_per_mille *
              static_cast<std::uint64_t>(metrics_.window_samples);

  if (metrics_.feedback_stale) {
    healthy_intervals_ = 0;
    // A stalled peer is one observation, not repeated independent congestion.
    // Stop further probing and reduce once until genuine feedback resumes.
    if (!stale_reduced_) {
      DecreaseRate(2);
      last_decrease_at_ = now;
      last_rate_change_at_ = now;
      stale_reduced_ = true;
    }
    return;
  }
  auto const fresh_loss =
      latest_loss_origin_ && *latest_loss_origin_ >= path_started_at_ &&
      (!last_decrease_at_ || *latest_loss_origin_ > *last_decrease_at_);
  if (queue_grew || new_latency_violation ||
      (new_losses && loss_over_target && fresh_loss)) {
    DecreaseRate(5);  // Reduce by 20%, no more than once per control interval.
    // Only actual congestion can make the observed delivery rate a cap. Using
    // it unconditionally would shrink an application-limited stream forever.
    if (delivery_rate_at_ &&
        !rate_controller_internal::Elapsed(now, *delivery_rate_at_,
                                           config_.feedback_timeout)) {
      auto const drain_rate =
          metrics_.delivery_rate - metrics_.delivery_rate / 20;
      metrics_.rate = std::max(config_.min_rate_bytes_per_second,
                               std::min(metrics_.rate, drain_rate));
    }
    last_decrease_at_ = now;
    last_rate_change_at_ = now;
    last_reduced_queue_age_ = local_queue_age;
    healthy_intervals_ = 0;
    return;
  }

  auto const loss_margin =
      std::max<std::uint32_t>(1, config_.max_loss_per_mille / 5);
  auto const safe_loss_limit = config_.max_loss_per_mille > loss_margin
                                   ? config_.max_loss_per_mille - loss_margin
                                   : 0;
  auto const healthy_loss =
      window_lost_packets_ * 1000ULL <=
      safe_loss_limit * static_cast<std::uint64_t>(metrics_.window_samples);
  auto const healthy_delay =
      metrics_.window_max_latency.count() <=
          static_cast<std::uint64_t>(config_.latency_target.count()) * 4 / 5 &&
      latest_latency_.count() <=
          static_cast<std::uint64_t>(config_.latency_target.count()) * 4 / 5;

  if (!healthy_loss || !healthy_delay || !new_outcomes || lower_blocked ||
      metrics_.history_limited || metrics_.feedback_incomplete) {
    healthy_intervals_ = 0;
    return;
  }
  if (!metrics_.window_valid) {
    healthy_intervals_ = 0;
    // A low-rate path may never collect min_samples in W. Requiring confidence
    // before every increase would deadlock recovery at the minimum rate. Make
    // a bounded exploratory increase only after fresh, loss-free end-to-end
    // evidence, and no more often than once per control interval. The window
    // remains explicitly low-confidence; empty windows and ACK-only traffic
    // cannot grow.
    auto const probe_interval = config_.control_interval;
    if (metrics_.window_samples != 0 && window_lost_packets_ == 0 &&
        rate_controller_internal::Elapsed(now, last_rate_change_at_,
                                          probe_interval)) {
      IncreaseRate(5);
      last_rate_change_at_ = now;
    }
    return;
  }
  if (++healthy_intervals_ >= 2) {
    IncreaseRate();
    last_rate_change_at_ = now;
    healthy_intervals_ = 0;
  }
}

void RateController::ResetPath(TimePoint now) {
  outcomes_.clear();
  truncated_through_.reset();
  latest_uncertain_at_.reset();
  last_decrease_at_.reset();
  latest_loss_origin_.reset();
  metrics_ = RateControllerMetrics{};
  metrics_.rate = valid_ ? config_.initial_rate_bytes_per_second : 0;
  latest_time_ = now;
  path_started_at_ = now;
  last_control_at_ = now;
  last_feedback_at_ = now;
  last_rate_change_at_ = now;
  outcome_generation_ = 0;
  evaluated_generation_ = 0;
  loss_generation_ = 0;
  evaluated_loss_generation_ = 0;
  stale_reduced_ = false;
  healthy_intervals_ = 0;
  latest_latency_ = Duration::zero();
  pending_latency_violation_ = false;
  last_queue_age_ = Duration::zero();
  last_reduced_queue_age_ = Duration::zero();
  window_lost_packets_ = 0;
  delay_samples_.clear();
  delivery_rate_at_.reset();
}

void RateController::TrimHistory(TimePoint now) {
  while (!outcomes_.empty() &&
         rate_controller_internal::Elapsed(now, outcomes_.front().completed_at,
                                           config_.loss_window)) {
    outcomes_.pop_front();
  }
  if (truncated_through_ &&
      rate_controller_internal::Elapsed(now, *truncated_through_,
                                        config_.loss_window)) {
    truncated_through_.reset();
  }
  if (latest_uncertain_at_ &&
      rate_controller_internal::Elapsed(now, *latest_uncertain_at_,
                                        config_.loss_window)) {
    latest_uncertain_at_.reset();
  }
}

void RateController::RefreshWindow(TimePoint now) {
  TrimHistory(now);
  metrics_.history_limited = truncated_through_.has_value();
  metrics_.feedback_incomplete = latest_uncertain_at_.has_value();
  metrics_.window_samples = outcomes_.size();
  metrics_.window_delivered_bytes = 0;
  metrics_.window_max_latency = Duration::zero();
  window_lost_packets_ = 0;
  for (auto const& outcome : outcomes_) {
    if (outcome.delivered_on_time) {
      metrics_.window_delivered_bytes = rate_controller_internal::SaturatingAdd(
          metrics_.window_delivered_bytes, outcome.payload_bytes);
    } else {
      ++window_lost_packets_;
    }
    metrics_.window_max_latency =
        std::max(metrics_.window_max_latency, outcome.latency);
  }
  // Report a conservative rounded-up diagnostic, but compare exact fractions
  // when making a rate decision or setting constraints_unmet.
  metrics_.window_loss_per_mille =
      metrics_.window_samples == 0
          ? 0
          : static_cast<std::uint32_t>(
                (window_lost_packets_ * 1000ULL + metrics_.window_samples - 1) /
                metrics_.window_samples);
  metrics_.window_valid = metrics_.window_samples >= config_.min_samples &&
                          !metrics_.history_limited &&
                          !metrics_.feedback_incomplete;
}

void RateController::RefreshConstraints(Duration local_queue_age) {
  auto const observed_loss_over_target =
      metrics_.window_samples != 0 &&
      window_lost_packets_ * 1000ULL >
          config_.max_loss_per_mille *
              static_cast<std::uint64_t>(metrics_.window_samples);
  metrics_.constraints_unmet =
      metrics_.feedback_stale || metrics_.feedback_incomplete ||
      observed_loss_over_target || latest_latency_ > config_.latency_target ||
      metrics_.window_max_latency > config_.latency_target ||
      local_queue_age > config_.latency_target;
}

void RateController::DecreaseRate(std::uint64_t divisor) {
  auto const step = std::max<std::uint64_t>(1, metrics_.rate / divisor);
  metrics_.rate = std::max(config_.min_rate_bytes_per_second,
                           metrics_.rate > step ? metrics_.rate - step : 0);
}

void RateController::IncreaseRate(std::uint64_t divisor) {
  auto const step = std::max<std::uint64_t>(1, metrics_.rate / divisor);
  metrics_.rate =
      std::min(config_.max_rate_bytes_per_second,
               rate_controller_internal::SaturatingAdd(metrics_.rate, step));
}

}  // namespace ae::adaptive_datagram
