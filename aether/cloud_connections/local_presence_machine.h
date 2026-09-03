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

#ifndef AETHER_CLOUD_CONNECTIONS_LOCAL_PRESENCE_MACHINE_H_
#define AETHER_CLOUD_CONNECTIONS_LOCAL_PRESENCE_MACHINE_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "aether/client_connectivity_policy.h"
#include "aether/cloud_connections/local_presence_schedule.h"
#include "aether/clock.h"

namespace ae {

// Production Local Presence orchestration used by PingCloudServers and tests.
//
// Client attempt_id / cycle_id are local only. The current cloud Ping API does
// not carry a schedule generation. If PREFIX1 is delayed on one adapter and
// PREFIX2 is applied first, a later PREFIX1 Pong is treated as same-cycle
// stats-only locally, but the server may still overwrite the listen schedule
// with PREFIX1's wire interval. Correct cross-adapter ordering would need a
// wire/server generation; that protocol extension is intentionally not made
// here.
class LocalPresenceMachine {
 public:
  struct Attempt {
    std::uint64_t attempt_id{};
    std::uint64_t cycle_id{};
    PingAttemptKind kind{PingAttemptKind::kInitial};
    TimePoint send_time{};
    Duration selected_rtt{};
    Duration sent_interval{};
    Duration desired_interval{};
    Duration sent_window{};
    TimePoint following_open_target{};
    TimePoint retry_deadline{};
    TimePoint cleanup_deadline{};
    bool scheduler_timed_out{false};
    bool awaiting_response{true};
  };

  struct SendSpec {
    std::uint64_t attempt_id{};
    std::uint64_t cycle_id{};
    PingAttemptKind kind{PingAttemptKind::kInitial};
    Duration wire_interval{};
    Duration desired_interval{};
    Duration rx_window{};
    Duration selected_rtt{};
    Duration hard_wait{};
    TimePoint following_open_target{};
    TimePoint retry_deadline{};
    TimePoint cleanup_deadline{};
    bool opens_current_window{false};
  };

  struct Tick {
    TimePoint next_wake{TimePoint::max()};
    bool want_send{false};
    SendSpec send{};
    bool restream{false};
    PresenceRestreamReason restream_reason{PresenceRestreamReason::kNone};
  };

  enum class PongDisposition : std::uint8_t {
    kUnknownAttempt = 0,
    kStatsOnly,
    kConfirmedSchedule,
  };

  struct PongOutcome {
    PongDisposition disposition{PongDisposition::kUnknownAttempt};
    ConfirmedReceiveSchedule schedule{};
  };

  struct Counters {
    int initial{};
    int prefix1{};
    int prefix2{};
    int retry{};
    int recovery{};
    int scheduler_timeouts{};
    int confirmed_pongs{};
    int late_pongs{};
    int restreams{};
    int recoveries_to_online{};
  };

  LocalPresenceMachine();

  void SetDesired(TimePoint now, RxTimingConf conf, std::uint8_t percentile);
  void SetOfflineDetectionTimeout(Duration timeout) noexcept;
  RxTimingConf const& desired() const noexcept { return desired_; }
  std::uint8_t percentile() const noexcept { return percentile_; }
  Duration offline_detection_timeout() const noexcept {
    return offline_detection_timeout_;
  }

  void RestoreConfirmed(TimePoint open, TimePoint close, Duration interval,
                        Duration window, TimePoint now,
                        Duration selected_rtt);
  void ArmInitial(TimePoint now);

  Tick TickNow(TimePoint now, Duration selected_rtt);

  void OnSendStarting();
  void OnAttemptSent(SendSpec spec, TimePoint send_time);
  void OnStartFailed(TimePoint now, Duration selected_rtt,
                     PresenceRestreamReason reason);

  PongOutcome OnPong(std::uint64_t attempt_id, std::uint64_t cycle_id,
                     TimePoint send_time, TimePoint pong_time,
                     Duration sent_interval, Duration sent_desired_interval,
                     Duration sent_window, TimePoint following_open_target,
                     Duration selected_rtt_after_sample);

  void OnHardFailure(std::uint64_t attempt_id, TimePoint now,
                     Duration selected_rtt, PresenceRestreamReason reason);
  void OnHardWaitExpired(std::uint64_t attempt_id, TimePoint now);

  void OnQuarantine(TimePoint now);
  void OnQuarantineReleased(TimePoint now, Duration selected_rtt);
  void OnRemoved();

  bool IsOnline(TimePoint now) const noexcept;
  bool has_confirmed_schedule() const noexcept { return has_confirmed_; }
  TimePoint confirmed_window_open() const noexcept { return confirmed_open_; }
  TimePoint confirmed_window_close() const noexcept { return confirmed_close_; }
  Duration confirmed_interval() const noexcept { return confirmed_interval_; }
  Duration confirmed_rx_window() const noexcept { return confirmed_window_; }
  TimePoint current_promised_close() const noexcept {
    return current_promised_close_;
  }
  bool current_window_blocker_held() const noexcept {
    return current_window_blocker_held_;
  }
  bool request_blocker_held() const noexcept { return request_blocker_held_; }
  bool CanSuspend() const noexcept {
    return !current_window_blocker_held_ && !request_blocker_held_;
  }
  std::size_t outstanding_attempt_count() const noexcept {
    return attempts_.size();
  }
  std::vector<Attempt> const& attempts() const noexcept { return attempts_; }
  Counters const& counters() const noexcept { return counters_; }
  bool quarantined() const noexcept { return quarantined_; }
  bool removed() const noexcept { return removed_; }
  bool config_change_pending() const noexcept { return config_pending_; }
  TimePoint last_following_target() const noexcept {
    return last_following_target_;
  }
  TimePoint PeekNextWake() const noexcept { return NextWake(TimePoint{}); }

 private:
  Attempt* FindAttempt(std::uint64_t attempt_id) noexcept;
  void EraseAttempt(std::uint64_t attempt_id);
  void BoundAttempts();
  void CleanupExpired(TimePoint now);
  void RecalcRequestBlocker();
  void ReleaseCurrentWindowIfDue(TimePoint now);
  void MarkSchedulerTimeouts(TimePoint now, Duration selected_rtt);
  void PlanAfterTimeout(Attempt const& timed_out, TimePoint now,
                        Duration selected_rtt);
  void PlanAfterConfirm(TimePoint now, Duration selected_rtt);
  void ArmSend(PingAttemptKind kind, TimePoint when);
  SendSpec BuildSendSpec(TimePoint now, Duration selected_rtt);
  bool HasActiveSchedulerAttempt() const noexcept;
  TimePoint NextWake(TimePoint now) const noexcept;
  TimePoint MakeCleanupDeadline(TimePoint send_time, Duration rtt) const;

  RxTimingConf desired_{RxTimingConf::Every(
      std::chrono::milliseconds{AE_PING_INTERVAL_MS})};
  std::uint8_t percentile_{kDefaultRttReliabilityPercentile};
  Duration offline_detection_timeout_{std::chrono::milliseconds{
      AE_OFFLINE_DETECTION_TIMEOUT_MS}};

  bool has_confirmed_{false};
  TimePoint confirmed_open_{};
  TimePoint confirmed_close_{};
  Duration confirmed_interval_{};
  Duration confirmed_window_{};
  bool config_pending_{false};

  bool current_window_blocker_held_{false};
  TimePoint current_promised_close_{};

  bool quarantined_{false};
  bool removed_{false};
  bool send_in_progress_{false};
  bool send_armed_{false};
  bool request_blocker_held_{false};
  bool restream_pending_{false};
  PresenceRestreamReason restream_reason_{PresenceRestreamReason::kNone};

  PingAttemptKind next_kind_{PingAttemptKind::kInitial};
  TimePoint next_send_time_{};

  std::uint64_t next_attempt_id_{0};
  std::uint64_t next_cycle_id_{0};
  std::uint64_t active_cycle_id_{0};
  TimePoint cycle_following_target_{};
  bool cycle_has_target_{false};
  bool cycle_confirmed_{false};
  TimePoint last_following_target_{};

  std::vector<Attempt> attempts_{};
  Counters counters_{};
};

}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_LOCAL_PRESENCE_MACHINE_H_
