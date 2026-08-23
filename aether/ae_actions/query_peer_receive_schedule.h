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

#ifndef AETHER_AE_ACTIONS_QUERY_PEER_RECEIVE_SCHEDULE_H_
#define AETHER_AE_ACTIONS_QUERY_PEER_RECEIVE_SCHEDULE_H_

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

#include "aether-miscpp/types/result.h"

#include "aether/ae_context.h"
#include "aether/actions/action.h"
#include "aether/clock.h"
#include "aether/cloud.h"
#include "aether/cloud_connections/cloud_request.h"
#include "aether/events/event_subscription.h"
#include "aether/events/events.h"
#include "aether/receive_schedule.h"
#include "aether/types/uid.h"

namespace ae {
class Client;
class CloudServerConnections;

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

inline TimePoint ComputeLocalAnchor(TimePoint query_begin,
                                    TimePoint query_end) noexcept {
  return query_begin + (query_end - query_begin) / 2;
}

inline TimePoint TimePointOffsetByMs(TimePoint anchor,
                                     std::int64_t delta_ms) noexcept {
  if (delta_ms == 0) {
    return anchor;
  }
  // Do not compute (TimePoint::max/min() - anchor): that difference overflows
  // system_clock::duration::rep on common platforms and falsely clamps to
  // min/max for ordinary millisecond offsets.
  using ClockDuration = typename TimePoint::duration;
  using Rep = typename ClockDuration::rep;
  auto const offset =
      std::chrono::duration_cast<ClockDuration>(std::chrono::milliseconds{
          delta_ms});
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

inline PeerReceiveSchedule MakePeerReceiveSchedule(
    TimePoint local_anchor, std::int64_t server_now_ms,
    std::int64_t last_read_timestamp_ms, std::int64_t delta_ms) noexcept {
  PeerReceiveSchedule schedule{};
  auto const age_ms =
      SaturatingSubI64(server_now_ms, last_read_timestamp_ms);
  schedule.last_ping = TimePointOffsetByMs(local_anchor, -age_ms);
  if (delta_ms > 0) {
    auto const remaining_ms = SaturatingSubI64(
        SaturatingAddI64(last_read_timestamp_ms, delta_ms), server_now_ms);
    schedule.next_ping_deadline =
        TimePointOffsetByMs(local_anchor, remaining_ms);
  }
  return schedule;
}

inline bool PeerReceiveScheduleValuesMalformed(
    std::int64_t server_now_ms, std::int64_t last_read_timestamp_ms) noexcept {
  return server_now_ms < 0 || last_read_timestamp_ms < 0;
}

enum class QueryPeerReceiveScheduleError : int {
  kGetCloudFailed = 1,
  kMainServerUnavailable = 2,
  kGetTimeUtcFailed = 3,
  kGetUapFailed = 4,
  kMalformedResponse = 5,
};

class QueryPeerReceiveSchedule final : public Action {
 public:
  using ResultEvent = Event<void(Result<PeerReceiveSchedule, int>)>;

  QueryPeerReceiveSchedule(AeContext const& ae_context, Client& client,
                           Uid peer_uid);

  AE_CLASS_NO_COPY_MOVE(QueryPeerReceiveSchedule)

  ResultEvent::Subscriber result_event() noexcept;

 private:
  void OnCloud(Result<Cloud::ptr, int> result);
  void StartQuery();
  void BeginAttempt();
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
  TimePoint query_begin_{};
  TimePoint local_anchor_{};
  std::int64_t server_now_ms_{0};
  std::int64_t last_ping_ms_{0};
  std::int64_t delta_ms_{0};
  int last_attempt_error_{0};
  CloudRequest* attempt_request_{nullptr};
  CloudServerConnection* attempt_sc_{nullptr};
};

}  // namespace ae

#endif  // AETHER_AE_ACTIONS_QUERY_PEER_RECEIVE_SCHEDULE_H_
