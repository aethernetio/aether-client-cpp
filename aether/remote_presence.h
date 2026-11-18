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

#include <algorithm>
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

enum class OnlineType : std::uint8_t {
  kNone = 0,
  kAethernet = 1,
  kLan = 2,
  kDirectIp = 4,
  kHolePunching = 8
};
constexpr OnlineType operator|(OnlineType a, OnlineType b) {
  return static_cast<OnlineType>(static_cast<unsigned>(a) |
                                 static_cast<unsigned>(b));
}
constexpr bool HasOnlineType(OnlineType types, OnlineType type) {
  return (static_cast<unsigned>(types) & static_cast<unsigned>(type)) != 0;
}

struct PeerPresence {
  PeerPresenceState state{PeerPresenceState::kUnknown};
  // Remote server observation is independent of this client's direct paths.
  PeerPresenceState aether_state{PeerPresenceState::kUnknown};
  OnlineType online_types{OnlineType::kNone};
  bool IsOnlineVia(OnlineType type) const {
    return HasOnlineType(online_types, type);
  }
};

struct ClientPresence {
  bool aether_online{};
  OnlineType online_types{OnlineType::kNone};
  bool IsOnline() const { return online_types != OnlineType::kNone; }
  bool IsOnlineVia(OnlineType type) const {
    return HasOnlineType(online_types, type);
  }
};

inline PeerPresence CombinePeerPresence(
    PeerPresence aether, OnlineType direct_types, bool aether_available,
    PeerPresenceState direct_state = PeerPresenceState::kUnknown) {
  aether.online_types = direct_types;
  if (aether_available && aether.aether_state == PeerPresenceState::kOnline) {
    aether.online_types = aether.online_types | OnlineType::kAethernet;
  }
  aether.state = aether.online_types != OnlineType::kNone
                     ? PeerPresenceState::kOnline
                 : aether.aether_state == PeerPresenceState::kUnknown
                     ? PeerPresenceState::kUnknown
                     : PeerPresenceState::kOffline;
  if (aether.state == PeerPresenceState::kUnknown && !aether_available &&
      direct_state == PeerPresenceState::kOffline) {
    aether.state = PeerPresenceState::kOffline;
  }
  return aether;
}

// Per authoritative usable server contribution for reachability aggregation.
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

// Classify one authoritative server response. A zero delta means that the
// server has no announced next Ping; only a known, expired promise is Offline.
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
    return RemoteServerPresence::kUnknown;
  }
  auto const expected = ProjectRemoteExpectedOpen(query_send, response_receive,
                                                  timing.next_ping_delta_ms);
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

// Match local reachability: a fresh Online from any authoritative server wins.
// Offline requires every usable authoritative contribution to be Offline.
// Missing/unknown evidence never overrides an Online or proves Offline.
inline PeerPresence AggregateRemotePresence(
    std::vector<RemoteServerPresenceSample> const& samples) noexcept {
  PeerPresence out{};
  std::size_t usable = 0;
  std::size_t online = 0;
  std::size_t offline = 0;
  bool any_unknown = false;
  for (auto const& sample : samples) {
    if (sample.status == RemoteServerPresence::kExcluded) {
      continue;
    }
    ++usable;
    if (sample.status == RemoteServerPresence::kOffline) {
      ++offline;
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
  if (online > 0) {
    out.state = PeerPresenceState::kOnline;
    out.aether_state = out.state;
    out.online_types = OnlineType::kAethernet;
    return out;
  }
  if (offline == usable && !any_unknown) {
    out.state = PeerPresenceState::kOffline;
    out.aether_state = out.state;
    return out;
  }
  out.state = PeerPresenceState::kUnknown;
  return out;
}

inline bool RemotePresenceCanEarlyCompleteOffline(
    std::vector<RemoteServerPresenceSample> const& samples) noexcept {
  return AggregateRemotePresence(samples).state == PeerPresenceState::kOffline;
}

inline bool RemotePresenceReadyForOnline(
    std::vector<RemoteServerPresenceSample> const& samples) noexcept {
  auto const aggregated = AggregateRemotePresence(samples);
  return aggregated.state == PeerPresenceState::kOnline;
}

inline Duration DefaultOfflineDetectionTimeout() noexcept {
  return std::chrono::milliseconds{AE_OFFLINE_DETECTION_TIMEOUT_MS};
}

// Local Presence (PingCloudServers + RequestPolicy::All) only maintains
// schedules on selected_servers(), which is bounded by
// AE_CLOUD_MAX_SERVER_CONNECTIONS. Remote Presence must query that same
// contract set — not an observer's own cloud, and not a silent subset smaller
// than the peer Presence obligation when peer cloud size <= max_connections.
inline std::vector<ServerId> AuthoritativePresenceServerIds(
    std::vector<ServerId> const& peer_cloud_ids_priority_order,
    std::size_t max_connections) noexcept {
  std::vector<ServerId> out;
  if (max_connections == 0) {
    return out;
  }
  auto const n =
      std::min(peer_cloud_ids_priority_order.size(), max_connections);
  out.assign(
      peer_cloud_ids_priority_order.begin(),
      peer_cloud_ids_priority_order.begin() + static_cast<std::ptrdiff_t>(n));
  return out;
}

// Never substitute the observer/requester cloud for the peer Personal Cloud.
inline bool AllowObserverCloudFallbackForPeerPresence() noexcept {
  return false;
}

}  // namespace ae

#endif  // AETHER_REMOTE_PRESENCE_H_
