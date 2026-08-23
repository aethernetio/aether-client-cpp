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

#include <optional>

#include "aether/clock.h"

namespace ae {

struct ReceiveSchedule {
  Duration ping_interval{};
  Duration receive_window{};
};

// Library TimePoint / ae::Now() local timeline only (relative offsets; no
// Unix-epoch wall remapping of server timestamps).
struct PeerReceiveSchedule {
  TimePoint last_ping{};
  std::optional<TimePoint> next_ping_deadline{};
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
