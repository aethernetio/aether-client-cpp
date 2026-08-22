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

#ifndef AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_TYPES_H_
#define AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_TYPES_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ae::bench::dw {

inline constexpr std::uint32_t kBenchMagic = 0x424D3031u;  // 'BM01'
inline constexpr std::uint8_t kBenchVersion = 1;
inline constexpr std::uint32_t kIpcMagic = 0x41445742u;  // 'ADWB'
inline constexpr std::uint8_t kIpcVersion = 1;

enum class Side : std::uint8_t { kCoordinator = 0, kA = 1, kB = 2 };

enum class Direction : std::uint8_t { kAtoB = 0, kBtoA = 1 };

enum class Bucket : std::uint8_t {
  kInsideEarly = 0,
  kInsideLate = 1,
  kOutsideEarly = 2,
  kOutsideLate = 3,
  kBeforeNextPing = 4,
  kUnknown = 255,
};

enum class Classification : std::uint8_t {
  kPushInsideWindow = 0,
  kDeferredDespiteActiveWindow = 1,
  kPushOutsideWindow = 2,
  kDeliveredAtNextWindow = 3,
  kLateAfterNextWindow = 4,
  kPulledExplicitly = 5,
  kLost = 6,
  kDuplicate = 7,
  kInvalid = 8,
};

enum class EventKind : std::uint8_t {
  kPingScheduled = 0,
  kPingWriteBegin = 1,
  kPingServerAck = 2,
  kRxWindowOpen = 3,
  kRxWindowClose = 4,
  kNextPingScheduled = 5,
  kMessageSendCommand = 10,
  kMessageSendCall = 11,
  kMessageServerAccepted = 12,
  kMessageReceived = 13,
  kPullRequest = 14,
  kUapServerNow = 20,
  kUapLastRead = 21,
  kUapDelta = 22,
  kUapExpectedNext = 23,
  kChildReady = 30,
  kWarmupDone = 31,
  kAck = 32,
  kError = 33,
};

enum class IpcType : std::uint8_t {
  kCalibPing = 1,
  kCalibPong = 2,
  kChildReady = 3,
  kShutdown = 4,
  kApplyTiming = 5,
  kUidReport = 6,
  kWaitWarmupPings = 7,
  kWarmupPingsDone = 8,
  kUapVerify = 9,
  kUapResult = 10,
  kWarmupMessage = 11,
  kWarmupMessageDone = 12,
  kSendMessage = 13,
  kPullMessages = 14,
  kEvent = 15,
  kFlushTrace = 16,
  kSetPeerUid = 17,
  kAck = 18,
};

struct TimingConfig {
  std::int64_t actual_ping_interval_ms{0};
  std::int64_t announced_next_ping_ms{0};
  std::int64_t rx_window_ms{0};
};

struct MatrixConfig {
  std::string id;
  TimingConfig timing;
  std::vector<Bucket> buckets;
  int samples_per_bucket{5};
};

struct SampleRecord {
  std::string run_id;
  Direction direction{Direction::kAtoB};
  std::string config_id;
  int sample_id{0};
  std::uint32_t cycle_id{0};
  std::uint16_t server_id{0};
  TimingConfig timing{};
  Bucket target_bucket{Bucket::kUnknown};
  Bucket actual_accept_bucket{Bucket::kUnknown};
  std::int64_t window_open_us{0};
  std::int64_t window_close_us{0};
  std::int64_t send_command_us{0};
  std::int64_t send_call_us{0};
  std::int64_t server_accept_us{0};
  std::int64_t receive_us{0};
  std::int64_t next_window_open_us{0};
  std::int64_t pull_request_us{0};
  double send_to_accept_ms{0};
  double accept_to_receive_ms{0};
  double send_to_receive_ms{0};
  Classification classification{Classification::kInvalid};
  int duplicate_count{0};
  bool valid{false};
  std::string invalid_reason;
  bool pull_control{false};
};

inline constexpr std::string_view BucketName(Bucket b) {
  switch (b) {
    case Bucket::kInsideEarly:
      return "INSIDE_EARLY";
    case Bucket::kInsideLate:
      return "INSIDE_LATE";
    case Bucket::kOutsideEarly:
      return "OUTSIDE_EARLY";
    case Bucket::kOutsideLate:
      return "OUTSIDE_LATE";
    case Bucket::kBeforeNextPing:
      return "BEFORE_NEXT_PING";
    default:
      return "UNKNOWN";
  }
}

inline constexpr std::string_view ClassificationName(Classification c) {
  switch (c) {
    case Classification::kPushInsideWindow:
      return "PUSH_INSIDE_WINDOW";
    case Classification::kDeferredDespiteActiveWindow:
      return "DEFERRED_DESPITE_ACTIVE_WINDOW";
    case Classification::kPushOutsideWindow:
      return "PUSH_OUTSIDE_WINDOW";
    case Classification::kDeliveredAtNextWindow:
      return "DELIVERED_AT_NEXT_WINDOW";
    case Classification::kLateAfterNextWindow:
      return "LATE_AFTER_NEXT_WINDOW";
    case Classification::kPulledExplicitly:
      return "PULLED_EXPLICITLY";
    case Classification::kLost:
      return "LOST";
    case Classification::kDuplicate:
      return "DUPLICATE";
    default:
      return "INVALID";
  }
}

inline constexpr std::string_view DirectionName(Direction d) {
  return d == Direction::kAtoB ? "A_TO_B" : "B_TO_A";
}

}  // namespace ae::bench::dw

#endif  // AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_TYPES_H_
