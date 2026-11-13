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

#ifndef AETHER_ADAPTIVE_DATAGRAM_RATE_CONTROLLER_H_
#define AETHER_ADAPTIVE_DATAGRAM_RATE_CONTROLLER_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>

#include "aether/adaptive_datagram/adaptive_datagram_config.h"
#include "aether/clock.h"

namespace ae::adaptive_datagram {

struct RateControllerMetrics {
  // Samples belong to the completed-at interval (now - W, now]. An empty or
  // undersampled window is unknown; its displayed zero is not a loss guarantee.
  std::uint32_t window_loss_per_mille{0};
  std::size_t window_samples{0};
  std::uint64_t window_delivered_bytes{0};
  bool window_valid{false};
  Duration window_max_latency{0};
  Duration latest_latency{0};
  Duration smoothed_latency{0};
  Duration base_latency{0};
  std::uint64_t delivery_rate{0};
  std::uint64_t delivered_bytes{0};
  std::uint64_t total_outcomes{0};
  std::uint64_t total_lost_packets{0};
  Duration rtt{0};
  std::uint64_t rate{0};
  bool constraints_unmet{false};
  bool feedback_stale{false};
  // Some original datagrams in W have no confirmed end-to-end outcome. This is
  // uncertainty, not measured network loss, and cannot authorize rate probing.
  bool feedback_incomplete{false};
  // At least one sample still in W was evicted due to the memory bound. The
  // retained fraction is diagnostic only and cannot authorize a rate increase.
  bool history_limited{false};
};

// A clock-injected runtime controller: no Now(), sleeping, sockets, or
// scheduler. A valid Config is a constructor precondition. Invalid
// configuration asserts in Debug and leaves the rate at zero in release builds.
class RateController {
 public:
  RateController(Config const& config, TimePoint now);

  // Only a final end-to-end outcome of an actually transmitted datagram may
  // call this method. Local expiry, cancellation, or a failed socket write is
  // not network loss. A successful local write or relay ACK is not delivery.
  // A deadline loss is final even if its datagram subsequently arrives.
  // latency measures the path after wire submission, excluding the local
  // application's admission/pacing queue; report that queue separately.
  // Supplying the original sent_at prevents delayed evidence from backing off
  // a rate already corrected since that packet was sent. Older callers that
  // omit it treat completion time as the evidence origin.
  void RecordOutcome(TimePoint completed_at, std::size_t payload_bytes,
                     bool delivered_on_time, Duration latency,
                     std::optional<TimePoint> sent_at = std::nullopt);
  void RecordUnconfirmed(TimePoint completed_at);
  // Live end-to-end delay feedback protects the path before an outcome matures.
  // Call only with a clock-corrected, trustworthy one-way latency sample.
  // The caller must deduplicate repeated reports of the same sample.
  void ObserveLatency(Duration latency,
                      std::optional<TimePoint> sent_at = std::nullopt);
  void FeedbackReceived(TimePoint now, Duration rtt);
  // Unique acknowledged wire bytes divided by a measurement interval. This
  // is a lower bound on capacity when the source is application limited.
  void ObserveDeliveryRate(TimePoint now, std::uint64_t bytes_per_second);
  // A busy application queue is demand, not evidence of network congestion.
  // Set lower_blocked only for an actual stalled lower-transport write. Local
  // queue age by itself neither lowers the wire rate nor prohibits probing.
  void Update(TimePoint now, Duration local_queue_age,
              bool lower_blocked = false);
  void ResetPath(TimePoint now);

  // window_* and history_limited are snapshots refreshed by Update(). Lifetime
  // counters, feedback state, and latest latency update with their
  // observations.
  [[nodiscard]] RateControllerMetrics const& metrics() const {
    return metrics_;
  }
  [[nodiscard]] std::uint64_t rate_bytes_per_second() const {
    return metrics_.rate;
  }

 private:
  struct Outcome {
    TimePoint completed_at;
    std::size_t payload_bytes;
    bool delivered_on_time;
    Duration latency;
  };

  void RefreshWindow(TimePoint now);
  void TrimHistory(TimePoint now);
  void RefreshConstraints(Duration local_queue_age);
  void DecreaseRate(std::uint64_t divisor);
  void IncreaseRate(std::uint64_t divisor = 20);

  Config config_;
  bool valid_{false};
  RateControllerMetrics metrics_;
  std::deque<Outcome> outcomes_;
  std::optional<TimePoint> truncated_through_;
  std::optional<TimePoint> latest_uncertain_at_;
  std::optional<TimePoint> last_decrease_at_;
  std::optional<TimePoint> latest_loss_origin_;
  TimePoint latest_time_;
  TimePoint path_started_at_;
  TimePoint last_control_at_;
  TimePoint last_feedback_at_;
  TimePoint last_rate_change_at_;
  std::uint64_t outcome_generation_{0};
  std::uint64_t evaluated_generation_{0};
  std::uint64_t loss_generation_{0};
  std::uint64_t evaluated_loss_generation_{0};
  bool stale_reduced_{false};
  std::uint32_t healthy_intervals_{0};
  Duration latest_latency_{0};
  bool pending_latency_violation_{false};
  Duration last_queue_age_{0};
  Duration last_reduced_queue_age_{0};
  std::size_t window_lost_packets_{0};
  std::deque<std::pair<TimePoint, Duration>> delay_samples_;
  std::optional<TimePoint> delivery_rate_at_;
};

}  // namespace ae::adaptive_datagram

#endif  // AETHER_ADAPTIVE_DATAGRAM_RATE_CONTROLLER_H_
