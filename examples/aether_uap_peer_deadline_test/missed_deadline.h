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

#ifndef EXAMPLES_AETHER_UAP_PEER_DEADLINE_TEST_MISSED_DEADLINE_H_
#define EXAMPLES_AETHER_UAP_PEER_DEADLINE_TEST_MISSED_DEADLINE_H_

#include <chrono>

#include "aether/receive_schedule.h"

namespace ae::test_uap_peer_deadline {

// PeerReceiveSchedule TimePoints are local-anchor converted per query, so tiny
// cross-query drift is expected. Treat only large moves as a new ping.
inline constexpr auto kLastPingAdvanceEpsilon = std::chrono::milliseconds{500};

inline bool IsLastOnlineAdvanced(TimePoint previous_last_online,
                                 TimePoint current_last_online) noexcept {
  return current_last_online > previous_last_online + kLastPingAdvanceEpsilon;
}

// Test-local helper. Production classification is PeerScheduleState.
inline bool IsMissedDeadline(PeerReceiveSchedule const& previous,
                             PeerReceiveSchedule const& current,
                             TimePoint now) noexcept {
  if (current.state == PeerScheduleState::kMissedDeadline) {
    return true;
  }
  if (!previous.next_ping_deadline.has_value()) {
    return false;
  }
  if (!(now > *previous.next_ping_deadline)) {
    return false;
  }
  return !IsLastOnlineAdvanced(previous.last_online, current.last_online);
}

}  // namespace ae::test_uap_peer_deadline

#endif  // EXAMPLES_AETHER_UAP_PEER_DEADLINE_TEST_MISSED_DEADLINE_H_
