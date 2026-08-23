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

#ifndef AETHER_UAP_DELIVERY_TIMING_BENCH_COMMON_BENCH_TYPES_H_
#define AETHER_UAP_DELIVERY_TIMING_BENCH_COMMON_BENCH_TYPES_H_

#include <cstdint>
#include <string>

namespace ae::bench::uap {

inline constexpr std::uint32_t kIpcMagic = 0x41555049u;  // 'AUPI'
inline constexpr std::uint8_t kIpcVersion = 1;

enum class Side : std::uint8_t { kCoordinator = 0, kA = 1, kB = 2 };

enum class EventKind : std::uint8_t {
  kAck = 0,
  kError = 1,
  kChildReady = 2,
  kWarmupDone = 3,
  kSampleSent = 4,
  kSampleReceived = 5,
  kSampleSkipped = 6,
};

enum class BenchProtocol : std::uint8_t {
  kUnknown = 0,
  kUdp = 1,
  kTcp = 2,
};

enum class UdpProofPath : std::uint8_t {
  kOwn = 1,
  kDestination = 2,
};

enum class IpcType : std::uint8_t {
  kChildReady = 1,
  kUidReport = 2,
  kSetPeerUid = 3,
  kWaitWarmup = 4,
  kWarmupDone = 5,
  kRunSample = 6,
  kSampleResult = 7,
  kEvent = 8,
  kShutdown = 9,
  kAck = 10,
  kUdpProof = 11,
};

struct SampleRecord {
  std::uint32_t sequence{0};
  std::int64_t offset_ms{0};
  std::int64_t last_ping_steady_us{0};
  std::int64_t next_ping_deadline_steady_us{-1};
  std::uint64_t send_qpc{0};
  std::uint64_t receive_qpc{0};
  double delivery_ms{0};
  int duplicate_count{0};
  bool valid{false};
  std::string invalid_reason;
};

}  // namespace ae::bench::uap

#endif  // AETHER_UAP_DELIVERY_TIMING_BENCH_COMMON_BENCH_TYPES_H_
