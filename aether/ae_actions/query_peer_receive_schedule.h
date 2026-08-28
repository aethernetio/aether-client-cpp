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

#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "aether-miscpp/types/result.h"

#include "aether/ae_context.h"
#include "aether/actions/action.h"
#include "aether/clock.h"
#include "aether/cloud.h"
#include "aether/cloud_connections/cloud_request.h"
#include "aether/events/event_subscription.h"
#include "aether/events/events.h"
#include "aether/receive_schedule.h"
#include "aether/types/server_id.h"
#include "aether/types/uid.h"
#include "aether/work_cloud_api/client_timing.h"

namespace ae {
class Client;
class CloudServerConnections;
class CloudServerConnection;

inline Duration FallbackOneWayPingEstimate() noexcept {
  return std::chrono::duration_cast<Duration>(std::chrono::milliseconds{100});
}

inline Duration OneWayPingEstimate(bool stats_empty,
                                   Duration min_rtt) noexcept {
  if (stats_empty) {
    return FallbackOneWayPingEstimate();
  }
  return min_rtt / 2;
}

inline TimePoint TimePointOffsetByMs(TimePoint anchor,
                                     std::int64_t delta_ms) noexcept {
  if (delta_ms == 0) {
    return anchor;
  }
  using ClockDuration = typename TimePoint::duration;
  using Rep = typename ClockDuration::rep;
  auto const max_safe_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
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

struct ConvertedServerTiming {
  ServerId server_id{};
  TimePoint last_online{};
  std::optional<TimePoint> next_ping_deadline{};
  PeerScheduleState state{PeerScheduleState::kUnknown};
};

inline ConvertedServerTiming ConvertClientTiming(
    TimePoint qsend, Duration one_way, ClientTiming const& timing,
    ServerId server_id = {}, Duration receive_window = Duration{},
    Duration ping_interval = Duration{}) noexcept {
  auto const qserver = qsend + one_way;
  ConvertedServerTiming out;
  out.server_id = server_id;
  out.last_online =
      TimePointOffsetByMs(qserver, timing.last_connect_delta_ms);
  if (timing.next_ping_delta_ms > 0) {
    out.next_ping_deadline =
        TimePointOffsetByMs(qserver, timing.next_ping_delta_ms);
    out.state = PeerScheduleState::kExpected;
  } else if (timing.next_ping_delta_ms < 0) {
    out.next_ping_deadline =
        TimePointOffsetByMs(qserver, timing.next_ping_delta_ms);
    auto const since_last_online = qserver - out.last_online;
    if (receive_window > Duration{} && ping_interval > Duration{} &&
        since_last_online >= Duration{} &&
        since_last_online < receive_window) {
      // Server reports a past nominal ping deadline while last activity is still
      // inside the configured receive window (query/deadline boundary race).
      out.state = PeerScheduleState::kExpected;
      out.next_ping_deadline = out.last_online + ping_interval;
    } else {
      out.state = PeerScheduleState::kMissedDeadline;
    }
  } else {
    out.next_ping_deadline = std::nullopt;
    out.state = PeerScheduleState::kUnknown;
  }
  return out;
}

inline std::optional<PeerReceiveSchedule> AggregatePeerTimings(
    std::vector<ConvertedServerTiming> const& samples) noexcept {
  if (samples.empty()) {
    return std::nullopt;
  }

  PeerReceiveSchedule out{};
  out.last_online = samples.front().last_online;

  bool any_future = false;
  bool any_unknown = false;
  bool any_missed = false;
  TimePoint latest_future{};
  TimePoint latest_missed{};

  for (auto const& sample : samples) {
    if (sample.last_online > out.last_online) {
      out.last_online = sample.last_online;
    }
    if (sample.state == PeerScheduleState::kExpected &&
        sample.next_ping_deadline.has_value()) {
      if (!any_future || *sample.next_ping_deadline > latest_future) {
        latest_future = *sample.next_ping_deadline;
      }
      any_future = true;
    } else if (sample.state == PeerScheduleState::kUnknown) {
      any_unknown = true;
    } else if (sample.state == PeerScheduleState::kMissedDeadline &&
               sample.next_ping_deadline.has_value()) {
      if (!any_missed || *sample.next_ping_deadline > latest_missed) {
        latest_missed = *sample.next_ping_deadline;
      }
      any_missed = true;
    }
  }

  if (any_future) {
    out.state = PeerScheduleState::kExpected;
    out.next_ping_deadline = latest_future;
  } else if (any_unknown) {
    out.state = PeerScheduleState::kUnknown;
    out.next_ping_deadline = std::nullopt;
  } else {
    out.state = PeerScheduleState::kMissedDeadline;
    out.next_ping_deadline = latest_missed;
  }
  return out;
}

enum class ServerTimingAttemptStatus {
  kInFlight,
  kSuccess,
  kError,
};

struct ServerTimingAttempt {
  std::uint64_t send_generation{0};
  TimePoint qsend{};
  Duration one_way{};
  ServerTimingAttemptStatus status{ServerTimingAttemptStatus::kInFlight};
  ConvertedServerTiming converted{};
};

// Pure helper for unit tests: generation, stale ignore, per-server isolation.
struct PeerTimingQueryState {
  std::uint64_t query_generation{0};
  bool cancelled{false};
  bool completed{false};
  int user_callback_count{0};
  Duration receive_window{};
  Duration ping_interval{};
  std::map<ServerId, ServerTimingAttempt> attempts;

  std::uint64_t Begin() {
    ++query_generation;
    cancelled = false;
    completed = false;
    user_callback_count = 0;
    attempts.clear();
    return query_generation;
  }

  bool IsCurrentQuery(std::uint64_t generation) const noexcept {
    return !cancelled && generation == query_generation;
  }

  std::uint64_t RegisterSend(ServerId server_id, TimePoint qsend,
                             Duration one_way) {
    auto& attempt = attempts[server_id];
    ++attempt.send_generation;
    attempt.qsend = qsend;
    attempt.one_way = one_way;
    attempt.status = ServerTimingAttemptStatus::kInFlight;
    attempt.converted = {};
    return attempt.send_generation;
  }

  bool ApplyTiming(ServerId server_id, std::uint64_t send_generation,
                   ClientTiming const& timing) {
    if (cancelled || completed) {
      return false;
    }
    auto it = attempts.find(server_id);
    if (it == attempts.end() || it->second.send_generation != send_generation) {
      return false;
    }
    it->second.status = ServerTimingAttemptStatus::kSuccess;
    it->second.converted = ConvertClientTiming(
        it->second.qsend, it->second.one_way, timing, server_id,
        receive_window, ping_interval);
    return true;
  }

  bool ApplyError(ServerId server_id, std::uint64_t send_generation) {
    if (cancelled || completed) {
      return false;
    }
    auto it = attempts.find(server_id);
    if (it == attempts.end() || it->second.send_generation != send_generation) {
      return false;
    }
    it->second.status = ServerTimingAttemptStatus::kError;
    return true;
  }

  std::optional<PeerReceiveSchedule> TryAggregate() const {
    std::vector<ConvertedServerTiming> ok;
    ok.reserve(attempts.size());
    for (auto const& [id, attempt] : attempts) {
      if (attempt.status == ServerTimingAttemptStatus::kSuccess) {
        ok.push_back(attempt.converted);
      }
    }
    return AggregatePeerTimings(ok);
  }

  void Cancel() { cancelled = true; }
};

enum class QueryPeerReceiveScheduleError : int {
  kGetCloudFailed = 1,
  kNoWorkServerAvailable = 2,
  kGetClientTimingFailed = 3,
};

class QueryPeerReceiveSchedule final : public Action {
 public:
  using ResultEvent = Event<void(Result<PeerReceiveSchedule, int>)>;

  QueryPeerReceiveSchedule(AeContext const& ae_context, Client& client,
                           Uid peer_uid);
  ~QueryPeerReceiveSchedule() override;

  AE_CLASS_NO_COPY_MOVE(QueryPeerReceiveSchedule)

  ResultEvent::Subscriber result_event() noexcept;

 private:
  Duration OneWayEstimateFor(CloudServerConnection* sc) const;
  void OnCloud(Result<Cloud::ptr, int> result);
  void StartQuery();
  void OnServerTiming(CloudServerConnection* sc, std::uint64_t send_generation,
                      Result<ClientTiming, int> const& res);
  void MaybeComplete();
  void Complete(PeerReceiveSchedule const& schedule);
  void Fail(int code);

  AeContext ae_context_;
  Client* client_{nullptr};
  Uid peer_uid_{};
  ResultEvent result_event_;
  Subscription get_cloud_sub_;
  Subscription cloud_request_sub_;
  std::unique_ptr<CloudServerConnections> dest_cloud_;
  std::optional<CloudRequest> cloud_request_;
  std::map<ServerId, Subscription> timing_subs_;
  PeerTimingQueryState query_state_{};
  bool finished_{false};
};

}  // namespace ae

#endif  // AETHER_AE_ACTIONS_QUERY_PEER_RECEIVE_SCHEDULE_H_
