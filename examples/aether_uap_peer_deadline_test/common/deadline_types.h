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

#ifndef EXAMPLES_AETHER_UAP_PEER_DEADLINE_TEST_COMMON_DEADLINE_TYPES_H_
#define EXAMPLES_AETHER_UAP_PEER_DEADLINE_TEST_COMMON_DEADLINE_TYPES_H_

#include <cstdint>
#include <string>

namespace ae::test_uap_peer_deadline {

inline constexpr std::uint32_t kIpcMagic = 0x41555044u;  // 'AUPD'
inline constexpr std::uint8_t kIpcVersion = 1;

enum class Side : std::uint8_t { kCoordinator = 0, kA = 1, kB = 2 };

enum class IpcType : std::uint8_t {
  kChildReady = 1,
  kUidReport = 2,
  kSetPeerUid = 3,
  kStartCloud = 4,
  kCloudStarted = 5,
  kRunTest = 6,
  kRequestBobKill = 7,
  kBobKilled = 8,
  kRequestBobRestart = 9,
  kBobRestarted = 10,
  kTestDone = 11,
  kShutdown = 12,
  kAck = 13,
  kLogLine = 14,
  kFlushState = 15,
};

}  // namespace ae::test_uap_peer_deadline

#endif  // EXAMPLES_AETHER_UAP_PEER_DEADLINE_TEST_COMMON_DEADLINE_TYPES_H_
