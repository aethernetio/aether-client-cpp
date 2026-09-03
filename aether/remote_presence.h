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

#ifndef AETHER_REMOTE_PRESENCE_H_
#define AETHER_REMOTE_PRESENCE_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "aether/clock.h"
#include "aether/config.h"
#include "aether/types/server_id.h"
#include "aether/work_cloud_api/client_timing.h"

namespace ae {

enum class PeerPresenceState : std::uint8_t {
  kOnline = 0,
  kOffline,
  kUnknown,
};

struct PeerPresence {
  PeerPresenceState state{PeerPresenceState::kUnknown};
};

// Per authoritative usable server contribution for Remote AND aggregation.
enum class RemoteServerPresence : std::uint8_t {
  kOnline = 0,
  kOffline,
  // Query pending / failed after retries while server remains usable.
  kUnknown,
  // Quarantined / unselected / removed — excluded from aggregation.
  kExcluded,
};

struct RemoteServerPresenceSample {
  ServerId server_id{};
  RemoteServerPresence status{RemoteServerPresence::kUnknown};
  TimePoint expected_open{};
  TimePoint offline_deadline{};
  std::int64_t next_ping_delta_ms{};
  bool has_timing{false};
};

inline constexpr std::size_t kRemotePresenceQueryRetryCount{1};

inline TimePoint TimePointOffsetByMs(TimePoint anchor,
                                     std::int64_t delta_ms) noexcept {
  if (delta_ms == 0) {
    return anchor;
  }
  using ClockDuration = typename TimePoint::duration;
  using Rep = typename ClockDuration::rep;
  auto const max_safe_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ClockDuration{std::numeric_limits<Rep>::max() / 4})
          .count();
  if (max_safe_ms > 0) {
    if (delta_ms > max_safe_ms) {
      return TimePoint::max();
    }
    if (delta_ms < -max_safe_ms) {
      return TimePoint::min();
    }
  }
  auto const offset = std::chrono::duration_cast<ClockDuration>(
      std::chrono::milliseconds{delta_ms});
  auto const base = anchor.time_since_epoch().count();
  auto const add = offset.count();
  if (add > 0) {
    if (base > std::numeric_limits<Rep>::max() - add) {
      return TimePoint::max();
    }
  } else if (add < 0) {
    if (base < std::numeric_limits<Rep>::min() - add) {
      return TimePoint::min();
    }
  }
  return TimePoint{ClockDuration{static_cast<Rep>(base + add)}};
}

// midpoint = query_send + (response_receive - query_send) / 2
inline TimePoint QueryMidpoint(TimePoint query_send,
                               TimePoint response_receive) noexcept {
  if (response_receive <= query_send) {
    return query_send;
  }
  return query_send + (response_receive - query_send) / 2;
}

inline TimePoint ProjectRemoteExpectedOpen(
    TimePoint query_send, TimePoint response_receive,
    std::int64_t next_ping_delta_ms) noexcept {
  return TimePointOffsetByMs(QueryMidpoint(query_send, response_receive),
                             next_ping_delta_ms);
}

// Classify one authoritative server response. next_ping_delta == 0 means no
// future promise => Offline immediately (do not wait offline_detection_timeout).
inline RemoteServerPresence ClassifyRemoteServerPresence(
    TimePoint now, TimePoint query_send, TimePoint response_receive,
    ClientTiming const& timing, Duration offline_detection_timeout,
    TimePoint* expected_open_out = nullptr,
    TimePoint* offline_deadline_out = nullptr) noexcept {
  if (timing.next_ping_delta_ms == 0) {
    if (expected_open_out != nullptr) {
      *expected_open_out = {};
    }
    if (offline_deadline_out != nullptr) {
      *offline_deadline_out = {};
    }
    return RemoteServerPresence::kOffline;
  }
  auto const expected = ProjectRemoteExpectedOpen(
      query_send, response_receive, timing.next_ping_delta_ms);
  auto const deadline = expected + offline_detection_timeout;
  if (expected_open_out != nullptr) {
    *expected_open_out = expected;
  }
  if (offline_deadline_out != nullptr) {
    *offline_deadline_out = deadline;
  }
  if (now <= deadline) {
    return RemoteServerPresence::kOnline;
  }
  return RemoteServerPresence::kOffline;
}

// Remote aggregation AND over usable authoritative servers.
// Offline: any usable Offline.
// Online: usable_count > 0 and every usable sample Online.
// Unknown: usable_count == 0, or no Offline but not every usable Online.
inline PeerPresence AggregateRemotePresence(
    std::vector<RemoteServerPresenceSample> const& samples) noexcept {
  PeerPresence out{};
  std::size_t usable = 0;
  std::size_t online = 0;
  bool any_offline = false;
  bool any_unknown = false;
  for (auto const& sample : samples) {
    if (sample.status == RemoteServerPresence::kExcluded) {
      continue;
    }
    ++usable;
    if (sample.status == RemoteServerPresence::kOffline) {
      any_offline = true;
    } else if (sample.status == RemoteServerPresence::kOnline) {
      ++online;
    } else {
      any_unknown = true;
    }
  }
  if (usable == 0) {
    out.state = PeerPresenceState::kUnknown;
    return out;
  }
  if (any_offline) {
    out.state = PeerPresenceState::kOffline;
    return out;
  }
  if (online == usable && !any_unknown) {
    out.state = PeerPresenceState::kOnline;
    return out;
  }
  out.state = PeerPresenceState::kUnknown;
  return out;
}

inline bool RemotePresenceCanEarlyCompleteOffline(
    std::vector<RemoteServerPresenceSample> const& samples) noexcept {
  for (auto const& sample : samples) {
    if (sample.status == RemoteServerPresence::kOffline) {
      return true;
    }
  }
  return false;
}

inline bool RemotePresenceReadyForOnline(
    std::vector<RemoteServerPresenceSample> const& samples) noexcept {
  auto const aggregated = AggregateRemotePresence(samples);
  return aggregated.state == PeerPresenceState::kOnline;
}

inline Duration DefaultOfflineDetectionTimeout() noexcept {
  return std::chrono::milliseconds{AE_OFFLINE_DETECTION_TIMEOUT_MS};
}

}  // namespace ae

#endif  // AETHER_REMOTE_PRESENCE_H_
