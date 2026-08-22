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

#ifndef AETHER_AE_ACTIONS_QUERY_PEER_PING_SCHEDULE_H_
#define AETHER_AE_ACTIONS_QUERY_PEER_PING_SCHEDULE_H_

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

#include "aether-miscpp/types/result.h"

#include "aether/ae_context.h"
#include "aether/actions/action.h"
#include "aether/cloud.h"
#include "aether/cloud_connections/cloud_request.h"
#include "aether/events/event_subscription.h"
#include "aether/events/events.h"
#include "aether/types/server_id.h"
#include "aether/types/uid.h"

namespace ae {
class Client;
class CloudServerConnections;
class CloudRequest;

inline constexpr std::int64_t kPeerPingScheduleGraceMs = 500;

inline std::int64_t SaturatingAddI64(std::int64_t a, std::int64_t b) noexcept {
  auto const max_v = std::numeric_limits<std::int64_t>::max();
  auto const min_v = std::numeric_limits<std::int64_t>::min();
  if (b >= 0) {
    if (a > max_v - b) {
      return max_v;
    }
  } else if (a < min_v - b) {
    return min_v;
  }
  return a + b;
}

inline std::int64_t SaturatingSubI64(std::int64_t a, std::int64_t b) noexcept {
  auto const max_v = std::numeric_limits<std::int64_t>::max();
  auto const min_v = std::numeric_limits<std::int64_t>::min();
  if (b >= 0) {
    if (a < min_v + b) {
      return min_v;
    }
  } else if (a > max_v + b) {
    return max_v;
  }
  return a - b;
}

struct PeerPingRemaining {
  std::int64_t remaining_ms{0};
  bool has_deadline{false};
};

// remaining = max(0, saturatingAdd(last, delta) - server_now)
// delta <= 0 means the peer did not promise a next visit.
inline PeerPingRemaining ComputePeerPingRemaining(
    std::int64_t last_read_timestamp_ms, std::int64_t delta_ms,
    std::int64_t server_now_ms) noexcept {
  if (delta_ms <= 0) {
    return {};
  }
  auto const expected =
      SaturatingAddI64(last_read_timestamp_ms, delta_ms);
  auto remaining = SaturatingSubI64(expected, server_now_ms);
  if (remaining < 0) {
    remaining = 0;
  }
  return PeerPingRemaining{remaining, true};
}

inline bool PeerPingScheduleValuesMalformed(
    std::int64_t server_now_ms, std::int64_t last_read_timestamp_ms) noexcept {
  return server_now_ms < 0 || last_read_timestamp_ms < 0;
}

// Saturating steady deadline: steady_now + remaining_ms + grace_ms, clamped to
// steady_clock::time_point::max() without signed duration overflow / UB.
inline std::chrono::steady_clock::time_point SafeSteadyDeadline(
    std::chrono::steady_clock::time_point steady_now, std::int64_t remaining_ms,
    std::int64_t grace_ms) noexcept {
  auto const max_tp = std::chrono::steady_clock::time_point::max();
  if (remaining_ms < 0) {
    remaining_ms = 0;
  }
  if (grace_ms < 0) {
    grace_ms = 0;
  }
  auto const total_ms = SaturatingAddI64(remaining_ms, grace_ms);
  if (total_ms <= 0) {
    return steady_now;
  }
  auto const max_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          max_tp - steady_now)
                          .count();
  if (max_ms <= 0) {
    return max_tp;
  }
  if (total_ms >= max_ms) {
    return max_tp;
  }
  return steady_now + std::chrono::milliseconds{total_ms};
}

struct PeerPingSchedule {
  std::int64_t server_now_ms{};
  std::int64_t last_ping_server_ms{};
  std::int64_t next_ping_delta_ms{};
  std::optional<std::chrono::steady_clock::time_point> local_deadline;
  ServerId server_id{};
};

inline PeerPingSchedule MakePeerPingSchedule(
    std::int64_t server_now_ms, std::int64_t last_ping_server_ms,
    std::int64_t next_ping_delta_ms, ServerId server_id,
    std::chrono::steady_clock::time_point steady_now) {
  PeerPingSchedule schedule{};
  schedule.server_now_ms = server_now_ms;
  schedule.last_ping_server_ms = last_ping_server_ms;
  schedule.next_ping_delta_ms = next_ping_delta_ms;
  schedule.server_id = server_id;
  auto const remaining = ComputePeerPingRemaining(
      last_ping_server_ms, next_ping_delta_ms, server_now_ms);
  if (remaining.has_deadline) {
    schedule.local_deadline = SafeSteadyDeadline(
        steady_now, remaining.remaining_ms, kPeerPingScheduleGraceMs);
  }
  return schedule;
}

// Query TARGET peer MainServer:
//   GetCloud(peer) -> CloudServerConnections -> RequestPolicy::MainServer
//   -> LoginApi.getTimeUTC + AuthorizedApi.getUap(peer)
enum class QueryPeerPingScheduleError : int {
  kGetCloudFailed = 1,
  kMainServerUnavailable = 2,
  kGetTimeUtcFailed = 3,
  kGetUapFailed = 4,
  kMalformedResponse = 5,
};

class QueryPeerPingSchedule final : public Action {
 public:
  using ResultEvent = Event<void(Result<PeerPingSchedule, int>)>;

  QueryPeerPingSchedule(AeContext const& ae_context, Client& client,
                        Uid peer_uid);

  AE_CLASS_NO_COPY_MOVE(QueryPeerPingSchedule)

  ResultEvent::Subscriber result_event() noexcept;

  // Diagnostics for tests / memory traces (change-only consumers).
  std::uint64_t attempt_generation() const noexcept {
    return attempt_generation_;
  }
  int last_attempt_error() const noexcept { return last_attempt_error_; }

 private:
  void OnCloud(Result<Cloud::ptr, int>&& result);
  void StartQuery();
  void BeginAttempt(ServerId server_id);
  void OnAttemptPromiseFailed(int error);
  void MaybeComplete(std::uint64_t generation);
  void Fail(int code);

  AeContext ae_context_;
  Client* client_{nullptr};
  Uid peer_uid_{};
  ResultEvent result_event_;
  Subscription get_cloud_sub_;
  Subscription cloud_request_sub_;
  std::unique_ptr<CloudServerConnections> dest_cloud_;
  std::optional<CloudRequest> cloud_request_;
  Subscription time_sub_;
  Subscription uap_sub_;

  std::uint64_t attempt_generation_{0};
  bool got_time_{false};
  bool got_uap_{false};
  bool attempt_failed_{false};
  bool finished_{false};
  std::int64_t server_now_ms_{0};
  std::int64_t last_ping_ms_{0};
  std::int64_t delta_ms_{0};
  ServerId attempt_server_id_{};
  int last_attempt_error_{0};
  CloudRequest* attempt_request_{nullptr};
  CloudServerConnection* attempt_sc_{nullptr};
};

}  // namespace ae

#endif  // AETHER_AE_ACTIONS_QUERY_PEER_PING_SCHEDULE_H_
