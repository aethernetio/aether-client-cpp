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

#include "aether/cloud_connections/local_presence_machine.h"

#include <algorithm>
#include <utility>

namespace ae {

namespace {

void CountKind(LocalPresenceMachine::Counters& counters,
               PingAttemptKind kind) noexcept {
  switch (kind) {
    case PingAttemptKind::kPrefix1:
      ++counters.prefix1;
      break;
    case PingAttemptKind::kPrefix2:
      ++counters.prefix2;
      break;
    case PingAttemptKind::kRetry:
      ++counters.retry;
      break;
    case PingAttemptKind::kRecovery:
      ++counters.recovery;
      break;
    case PingAttemptKind::kInitial:
      ++counters.initial;
      break;
  }
}

}  // namespace

LocalPresenceMachine::LocalPresenceMachine() = default;

void LocalPresenceMachine::SetDesired(TimePoint now, RxTimingConf conf,
                                      Percentile percentile) {
  if (conf.interval <= Duration{}) {
    conf.interval = std::chrono::milliseconds{AE_PING_INTERVAL_MS};
  }
  if (conf.rx_window <= Duration{}) {
    conf.rx_window = conf.interval;
  }
  auto const changed = (desired_.interval != conf.interval) ||
                       (desired_.rx_window != conf.rx_window);
  desired_ = conf;
  percentile_ = percentile;
  if (!changed) {
    return;
  }
  config_pending_ = true;
  if (!removed_ && !quarantined_ && !HasActiveSchedulerAttempt()) {
    ArmSend(PingAttemptKind::kInitial, now);
  }
}

void LocalPresenceMachine::SetOfflineDetectionTimeout(Duration timeout) noexcept {
  if (timeout <= Duration{}) {
    timeout = std::chrono::milliseconds{AE_OFFLINE_DETECTION_TIMEOUT_MS};
  }
  offline_detection_timeout_ = timeout;
}

void LocalPresenceMachine::ArmInitial(TimePoint now) {
  if (removed_ || quarantined_) {
    return;
  }
  ArmSend(PingAttemptKind::kInitial, now);
}

void LocalPresenceMachine::RestoreConfirmed(TimePoint open, TimePoint close,
                                            Duration interval, Duration window,
                                            TimePoint now,
                                            Duration selected_rtt) {
  if (removed_) {
    return;
  }
  has_confirmed_ = true;
  confirmed_open_ = open;
  confirmed_close_ = close;
  confirmed_interval_ = interval;
  confirmed_window_ = window;
  cycle_has_target_ = false;
  cycle_confirmed_ = true;
  PlanAfterConfirm(now, selected_rtt);
}

LocalPresenceMachine::Tick LocalPresenceMachine::TickNow(
    TimePoint now, Duration selected_rtt) {
  Tick out{};
  if (removed_) {
    request_blocker_held_ = false;
    current_window_blocker_held_ = false;
    return out;
  }

  ReleaseCurrentWindowIfDue(now);
  CleanupExpired(now);
  MarkSchedulerTimeouts(now, selected_rtt);
  RecalcRequestBlocker();

  if (restream_pending_) {
    out.restream = true;
    out.restream_reason = restream_reason_;
    restream_pending_ = false;
    restream_reason_ = PresenceRestreamReason::kNone;
  }

  if (!quarantined_ && send_armed_ && (now >= next_send_time_) &&
      !HasActiveSchedulerAttempt()) {
    out.want_send = true;
    out.send = BuildSendSpec(now, selected_rtt);
    send_in_progress_ = true;
    send_armed_ = false;
    RecalcRequestBlocker();
  }

  out.next_wake = NextWake(now);
  return out;
}

void LocalPresenceMachine::OnSendStarting() {
  send_in_progress_ = true;
  RecalcRequestBlocker();
}

void LocalPresenceMachine::OnAttemptSent(SendSpec spec, TimePoint send_time) {
  send_in_progress_ = false;

  Attempt attempt{};
  attempt.attempt_id = spec.attempt_id;
  attempt.cycle_id = spec.cycle_id;
  attempt.kind = spec.kind;
  attempt.send_time = send_time;
  attempt.selected_rtt = spec.selected_rtt;
  attempt.sent_interval = spec.wire_interval;
  attempt.desired_interval = spec.desired_interval;
  attempt.sent_window = spec.rx_window;
  attempt.following_open_target = spec.following_open_target;
  attempt.retry_deadline = send_time + spec.selected_rtt;
  attempt.cleanup_deadline = MakeCleanupDeadline(send_time, spec.selected_rtt);
  attempt.scheduler_timed_out = false;
  attempt.awaiting_response = true;
  attempts_.push_back(attempt);
  BoundAttempts();
  CountKind(counters_, spec.kind);

  if (spec.opens_current_window && has_confirmed_) {
    if ((spec.kind == PingAttemptKind::kPrefix1) ||
        !current_window_blocker_held_) {
      current_promised_close_ = confirmed_close_;
    }
    current_window_blocker_held_ = true;
  }
  RecalcRequestBlocker();
}

void LocalPresenceMachine::OnStartFailed(TimePoint now, Duration selected_rtt,
                                         PresenceRestreamReason reason) {
  send_in_progress_ = false;
  RecalcRequestBlocker();
  if (reason != PresenceRestreamReason::kNone) {
    restream_pending_ = true;
    restream_reason_ = reason;
    ++counters_.restreams;
  }
  ArmSend(PingAttemptKind::kRecovery, now + selected_rtt);
}

LocalPresenceMachine::PongOutcome LocalPresenceMachine::OnPong(
    std::uint64_t attempt_id, std::uint64_t cycle_id, TimePoint send_time,
    TimePoint pong_time, Duration sent_interval,
    Duration sent_desired_interval, Duration sent_window,
    TimePoint following_open_target, Duration selected_rtt_after_sample) {
  PongOutcome out{};
  auto* attempt = FindAttempt(attempt_id);
  if (attempt != nullptr) {
    cycle_id = attempt->cycle_id;
    sent_interval = attempt->sent_interval;
    sent_desired_interval = attempt->desired_interval;
    sent_window = attempt->sent_window;
    following_open_target = attempt->following_open_target;
    attempt->awaiting_response = false;
    EraseAttempt(attempt_id);
  }
  if (sent_desired_interval <= Duration{}) {
    sent_desired_interval = sent_interval;
  }
  RecalcRequestBlocker();

  out.schedule = MakeConfirmedSchedule(send_time, pong_time, sent_interval,
                                       sent_window, selected_rtt_after_sample);

  auto const same_cycle = (cycle_id == active_cycle_id_);
  if (!same_cycle || (cycle_confirmed_ && same_cycle)) {
    out.disposition = PongDisposition::kStatsOnly;
    ++counters_.late_pongs;
    if (same_cycle) {
      last_following_target_ = following_open_target;
    }
    return out;
  }

  auto const was_online = IsOnline(pong_time);
  if (sent_desired_interval <= Duration{}) {
    has_confirmed_ = false;
    confirmed_open_ = {};
    confirmed_close_ = {};
    confirmed_interval_ = {};
    confirmed_window_ = sent_window;
    config_pending_ = desired_.interval > Duration{};
    cycle_confirmed_ = true;
    active_cycle_id_ = cycle_id;
    last_following_target_ = following_open_target;
    ++counters_.confirmed_pongs;
    out.disposition = PongDisposition::kConfirmedSchedule;
    current_window_blocker_held_ = false;
    if (desired_.interval > Duration{}) {
      ArmSend(PingAttemptKind::kInitial, pong_time);
    } else {
      send_armed_ = false;
    }
    return out;
  }

  has_confirmed_ = true;
  confirmed_open_ = out.schedule.window_open_local;
  confirmed_close_ = out.schedule.window_close_local;
  confirmed_interval_ = sent_desired_interval;
  confirmed_window_ = sent_window;
  config_pending_ = (desired_.interval != sent_desired_interval) ||
                    (desired_.rx_window != sent_window);
  cycle_confirmed_ = true;
  active_cycle_id_ = cycle_id;
  last_following_target_ = following_open_target;
  ++counters_.confirmed_pongs;
  if (!was_online) {
    ++counters_.recoveries_to_online;
  }
  out.disposition = PongDisposition::kConfirmedSchedule;
  PlanAfterConfirm(pong_time, selected_rtt_after_sample);
  return out;
}

void LocalPresenceMachine::OnHardFailure(std::uint64_t attempt_id,
                                         TimePoint now, Duration selected_rtt,
                                         PresenceRestreamReason reason) {
  auto* attempt = FindAttempt(attempt_id);
  auto kind = PingAttemptKind::kRecovery;
  if (attempt != nullptr) {
    kind = attempt->kind;
    attempt->awaiting_response = false;
    EraseAttempt(attempt_id);
  }
  send_in_progress_ = false;
  RecalcRequestBlocker();
  restream_pending_ = true;
  restream_reason_ = reason;
  ++counters_.restreams;
  Attempt timed_out{};
  timed_out.kind = kind;
  if (has_confirmed_ && (now <= confirmed_close_) &&
      AttemptOpensPromisedWindow(kind)) {
    PlanAfterTimeout(timed_out, now, selected_rtt);
  } else {
    ArmSend(PingAttemptKind::kRecovery, now + selected_rtt);
  }
}

void LocalPresenceMachine::OnHardWaitExpired(std::uint64_t attempt_id,
                                             TimePoint now) {
  static_cast<void>(now);
  EraseAttempt(attempt_id);
  RecalcRequestBlocker();
}

void LocalPresenceMachine::OnQuarantine(TimePoint now) {
  quarantined_ = true;
  send_in_progress_ = false;
  send_armed_ = false;
  attempts_.clear();
  RecalcRequestBlocker();
  ReleaseCurrentWindowIfDue(now);
}

void LocalPresenceMachine::OnQuarantineReleased(TimePoint now,
                                                Duration selected_rtt) {
  quarantined_ = false;
  ArmSend(PingAttemptKind::kRecovery, now + selected_rtt);
}

void LocalPresenceMachine::OnRemoved() {
  removed_ = true;
  quarantined_ = false;
  send_in_progress_ = false;
  send_armed_ = false;
  has_confirmed_ = false;
  current_window_blocker_held_ = false;
  request_blocker_held_ = false;
  attempts_.clear();
}

bool LocalPresenceMachine::IsOnline(TimePoint now) const noexcept {
  if (removed_) {
    return false;
  }
  return IsLocalPresenceOnline(has_confirmed_, confirmed_interval_,
                               confirmed_open_, now, offline_detection_timeout_);
}

LocalPresenceMachine::Attempt* LocalPresenceMachine::FindAttempt(
    std::uint64_t attempt_id) noexcept {
  for (auto& attempt : attempts_) {
    if (attempt.attempt_id == attempt_id) {
      return &attempt;
    }
  }
  return nullptr;
}

void LocalPresenceMachine::EraseAttempt(std::uint64_t attempt_id) {
  attempts_.erase(std::remove_if(attempts_.begin(), attempts_.end(),
                                 [attempt_id](Attempt const& attempt) {
                                   return attempt.attempt_id == attempt_id;
                                 }),
                  attempts_.end());
}

void LocalPresenceMachine::BoundAttempts() {
  while (attempts_.size() > kMaxOutstandingPresenceAttempts) {
    auto it = std::find_if(attempts_.begin(), attempts_.end(),
                           [](Attempt const& attempt) {
                             return attempt.scheduler_timed_out ||
                                    !attempt.awaiting_response;
                           });
    if (it == attempts_.end()) {
      it = attempts_.begin();
    }
    attempts_.erase(it);
  }
}

void LocalPresenceMachine::CleanupExpired(TimePoint now) {
  attempts_.erase(std::remove_if(attempts_.begin(), attempts_.end(),
                                 [now](Attempt const& attempt) {
                                   return now >= attempt.cleanup_deadline;
                                 }),
                  attempts_.end());
}

void LocalPresenceMachine::RecalcRequestBlocker() {
  if (removed_) {
    request_blocker_held_ = false;
    return;
  }
  if (send_in_progress_ && !current_window_blocker_held_) {
    request_blocker_held_ = true;
    return;
  }
  for (auto const& attempt : attempts_) {
    if (attempt.awaiting_response &&
        !AttemptOpensPromisedWindow(attempt.kind)) {
      request_blocker_held_ = true;
      return;
    }
  }
  request_blocker_held_ = false;
}

void LocalPresenceMachine::ReleaseCurrentWindowIfDue(TimePoint now) {
  if (current_window_blocker_held_ && (now > current_promised_close_)) {
    current_window_blocker_held_ = false;
  }
}

void LocalPresenceMachine::MarkSchedulerTimeouts(TimePoint now,
                                                 Duration selected_rtt) {
  for (auto& attempt : attempts_) {
    if (!attempt.awaiting_response || attempt.scheduler_timed_out) {
      continue;
    }
    if (now < attempt.retry_deadline) {
      continue;
    }
    attempt.scheduler_timed_out = true;
    ++counters_.scheduler_timeouts;
    if (!send_armed_ && !send_in_progress_ && !quarantined_) {
      PlanAfterTimeout(attempt, now, selected_rtt);
    }
  }
}

void LocalPresenceMachine::PlanAfterTimeout(Attempt const& timed_out,
                                            TimePoint now,
                                            Duration selected_rtt) {
  if (has_confirmed_ && (now <= confirmed_close_)) {
    if (timed_out.kind == PingAttemptKind::kPrefix1) {
      auto prefix2 =
          ComputePrefix2Time(confirmed_open_, selected_rtt);
      if (prefix2 < now) {
        prefix2 = now;
      }
      ArmSend(PingAttemptKind::kPrefix2, prefix2);
      return;
    }
    ArmSend(PingAttemptKind::kRetry, now + selected_rtt);
    return;
  }
  ArmSend(PingAttemptKind::kRecovery, now + selected_rtt);
}

void LocalPresenceMachine::PlanAfterConfirm(TimePoint now,
                                            Duration selected_rtt) {
  cycle_has_target_ = false;
  if (config_pending_) {
    ArmSend(PingAttemptKind::kInitial, now);
    return;
  }
  auto prefix1 = ComputePrefix1Time(confirmed_open_, selected_rtt);
  if (prefix1 < now) {
    prefix1 = now;
  }
  ArmSend(PingAttemptKind::kPrefix1, prefix1);
}

void LocalPresenceMachine::ArmSend(PingAttemptKind kind, TimePoint when) {
  next_kind_ = kind;
  next_send_time_ = when;
  send_armed_ = true;
}

LocalPresenceMachine::SendSpec LocalPresenceMachine::BuildSendSpec(
    TimePoint now, Duration selected_rtt) {
  if (selected_rtt <= Duration{}) {
    selected_rtt = kLocalPresenceGuard;
  }

  SendSpec spec{};
  spec.attempt_id = ++next_attempt_id_;
  spec.kind = next_kind_;
  spec.selected_rtt = selected_rtt;
  spec.rx_window = desired_.rx_window;

  auto const start_new_cycle =
      (spec.kind == PingAttemptKind::kInitial) ||
      (spec.kind == PingAttemptKind::kRecovery) ||
      (spec.kind == PingAttemptKind::kPrefix1) || !cycle_has_target_;

  if (start_new_cycle) {
    active_cycle_id_ = ++next_cycle_id_;
    cycle_confirmed_ = false;
    cycle_has_target_ = true;
    if (has_confirmed_ && AttemptOpensPromisedWindow(spec.kind)) {
      cycle_following_target_ = confirmed_open_ + desired_.interval;
    } else {
      auto const a_estimated = now + OneWayFromRtt(selected_rtt);
      cycle_following_target_ = a_estimated + desired_.interval;
    }
  }

  spec.cycle_id = active_cycle_id_;
  auto plan = PlanWireInterval(now, selected_rtt, cycle_following_target_,
                               desired_.interval);
  cycle_following_target_ = plan.following_open_target;
  spec.following_open_target = plan.following_open_target;
  spec.wire_interval = plan.wire_interval;
  if (spec.wire_interval <= Duration{}) {
    spec.wire_interval = desired_.interval;
  }
  if (spec.wire_interval <= Duration{}) {
    spec.wire_interval = std::chrono::milliseconds{AE_PING_INTERVAL_MS};
  }
  spec.desired_interval = desired_.interval;
  spec.hard_wait =
      PresenceHardWait(selected_rtt, desired_.interval, desired_.rx_window);
  spec.retry_deadline = now + selected_rtt;
  spec.cleanup_deadline = MakeCleanupDeadline(now, selected_rtt);
  spec.opens_current_window =
      has_confirmed_ && AttemptOpensPromisedWindow(spec.kind);
  return spec;
}

bool LocalPresenceMachine::HasActiveSchedulerAttempt() const noexcept {
  if (send_in_progress_) {
    return true;
  }
  for (auto const& attempt : attempts_) {
    if (attempt.awaiting_response && !attempt.scheduler_timed_out) {
      return true;
    }
  }
  return false;
}

TimePoint LocalPresenceMachine::NextWake(TimePoint now) const noexcept {
  static_cast<void>(now);
  auto next = TimePoint::max();
  if (send_armed_) {
    next = std::min(next, next_send_time_);
  }
  if (current_window_blocker_held_) {
    next = std::min(next, current_promised_close_);
  }
  for (auto const& attempt : attempts_) {
    if (attempt.awaiting_response && !attempt.scheduler_timed_out) {
      next = std::min(next, attempt.retry_deadline);
    }
    next = std::min(next, attempt.cleanup_deadline);
  }
  return next;
}

TimePoint LocalPresenceMachine::MakeCleanupDeadline(TimePoint send_time,
                                                    Duration rtt) const {
  auto deadline = send_time + (rtt * 8);
  if (has_confirmed_) {
    auto const until_close = confirmed_close_ + rtt;
    if (until_close > deadline) {
      deadline = until_close;
    }
  }
  return deadline;
}

}  // namespace ae
