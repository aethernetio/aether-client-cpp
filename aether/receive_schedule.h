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

#ifndef AETHER_RECEIVE_SCHEDULE_H_
#define AETHER_RECEIVE_SCHEDULE_H_

#include <cstddef>
#include <cstdint>
#include <optional>

#include "aether/clock.h"

namespace ae {

inline constexpr std::uint8_t kDefaultPingRetryCount = 0;
inline constexpr std::uint8_t kMaxPingRetryCount = 8;

struct ReceiveSchedule {
  Duration ping_interval{};
  Duration receive_window{};
  // Additional same-cycle retries reserved in the pre-deadline send budget
  // before Tn. Does not cap post-deadline recovery retries after Tn.
  std::uint8_t ping_retry_count{kDefaultPingRetryCount};
};

enum class PeerScheduleState {
  kExpected,
  kMissedDeadline,
  kUnknown,
};

// Library TimePoint / ae::Now() local timeline only (relative offsets; no
// Unix-epoch wall remapping of server timestamps).
// last_online is converted from lastConnectDeltaMs (online/activity, not
// necessarily a ping).
//
// Server-scoped only: PeerScheduleState::kMissedDeadline means THIS server has
// not seen a newer scheduled ping yet. It is NOT a global Offline signal —
// another cloud server may still report kExpected.
struct PeerReceiveSchedule {
  TimePoint last_online{};
  std::optional<TimePoint> next_ping_deadline{};
  PeerScheduleState state{PeerScheduleState::kUnknown};
};

// Cloud-scoped presence (OR Online / AND Offline). Distinct from
// PeerReceiveSchedule, which answers one server's schedule view.
enum class PeerPresenceState : std::uint8_t {
  kOnline,
  kOffline,
  kUnknown,
};

struct PeerPresence {
  PeerPresenceState state{PeerPresenceState::kUnknown};
  std::optional<TimePoint> last_online;
  std::optional<TimePoint> next_ping_deadline;
};

struct PeerTimingQueryCoverage {
  std::size_t selected_server_count{0};
  std::size_t queried_server_count{0};
  std::size_t successful_server_count{0};
  std::size_t failed_server_count{0};
  std::size_t quarantined_skipped_count{0};
};

enum class SetReceiveScheduleError : int {
  kPingAlreadyStarted = 1,
};

enum class ReceiveSendPhase {
  kInsideReceiveWindow,
  kOutsideReceiveWindow,
};

inline ReceiveSendPhase ClassifyReceiveSendOffset(
    Duration offset_from_last_ping, Duration receive_window) noexcept {
  return offset_from_last_ping < receive_window
             ? ReceiveSendPhase::kInsideReceiveWindow
             : ReceiveSendPhase::kOutsideReceiveWindow;
}

}  // namespace ae

#endif  // AETHER_RECEIVE_SCHEDULE_H_
