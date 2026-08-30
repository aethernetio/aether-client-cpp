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
// Single-server ClientTiming -> PeerReceiveSchedule conversion.
// Does NOT use observer receive_window / ping_interval.
inline ConvertedServerTiming ConvertClientTiming(
    TimePoint qsend, Duration one_way, ClientTiming const& timing,
    ServerId server_id = {}) noexcept {
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
    out.state = PeerScheduleState::kMissedDeadline;
  } else {
    out.next_ping_deadline = std::nullopt;
    out.state = PeerScheduleState::kUnknown;
  }
  return out;
}
inline PeerReceiveSchedule ToPeerReceiveSchedule(
    ConvertedServerTiming const& converted) noexcept {
  PeerReceiveSchedule out{};
  out.last_online = converted.last_online;
  out.next_ping_deadline = converted.next_ping_deadline;
  out.state = converted.state;
  return out;
}
struct SelectedServerSnapshotItem {
  ServerId id{};
  bool quarantine{false};
  bool has_descriptor{true};
};
inline PeerTimingQueryCoverage BuildPeerTimingQuerySet(
    std::vector<SelectedServerSnapshotItem> const& selected,
    std::vector<ServerId>& query_set) noexcept {
  PeerTimingQueryCoverage cov;
  cov.selected_server_count = selected.size();
  query_set.clear();
  query_set.reserve(selected.size());
  for (auto const& item : selected) {
    if (item.quarantine) {
      ++cov.quarantined_skipped_count;
      continue;
    }
    if (!item.has_descriptor) {
      continue;
    }
    query_set.push_back(item.id);
  }
  cov.queried_server_count = query_set.size();
  return cov;
}
// Cloud-scoped presence aggregation.
// Online  = ANY Expected (OR). Completes as soon as one Expected is observed.
// Offline = ALL relevant servers successful MissedDeadline (AND).
// Unknown = otherwise (partial failure, Unknown sample, incomplete set).
// next_ping_deadline on early Online is taken from Expected servers observed
// so far (prefer max among them); presence latency beats full-cloud schedule
// metadata completeness.
struct PeerTimingAggregateContext {
  std::size_t expected_server_count{0};
  std::size_t success_count{0};
  std::size_t terminal_error_count{0};
  std::size_t unresolved_count{0};
  bool snapshot_incomplete{false};
  std::vector<ConvertedServerTiming> successes;
};
inline std::optional<PeerPresence> AggregatePeerPresence(
    PeerTimingAggregateContext const& ctx) noexcept {
  if (ctx.success_count == 0 || ctx.successes.empty()) {
    return std::nullopt;
  }
  PeerPresence out{};
  out.last_online = ctx.successes.front().last_online;
  bool any_expected = false;
  bool any_unknown = false;
  bool any_missed = false;
  TimePoint latest_expected{};
  TimePoint latest_missed{};
  for (auto const& sample : ctx.successes) {
    if (sample.last_online > *out.last_online) {
      out.last_online = sample.last_online;
    }
    if (sample.state == PeerScheduleState::kExpected &&
        sample.next_ping_deadline.has_value()) {
      if (!any_expected || *sample.next_ping_deadline > latest_expected) {
        latest_expected = *sample.next_ping_deadline;
      }
      any_expected = true;
    } else if (sample.state == PeerScheduleState::kUnknown) {
      any_unknown = true;
    } else if (sample.state == PeerScheduleState::kMissedDeadline &&
               sample.next_ping_deadline.has_value()) {
      if (!any_missed || *sample.next_ping_deadline > latest_missed) {
        latest_missed = *sample.next_ping_deadline;
      }
      any_missed = true;
    } else if (sample.state == PeerScheduleState::kMissedDeadline) {
      any_missed = true;
    }
  }
  if (any_expected) {
    out.state = PeerPresenceState::kOnline;
    out.next_ping_deadline = latest_expected;
    return out;
  }
  bool const incomplete =
      ctx.snapshot_incomplete || ctx.unresolved_count > 0 ||
      ctx.terminal_error_count > 0 || ctx.expected_server_count == 0 ||
      ctx.success_count < ctx.expected_server_count;
  if (any_unknown || incomplete) {
    out.state = PeerPresenceState::kUnknown;
    out.next_ping_deadline = std::nullopt;
    return out;
  }
  out.state = PeerPresenceState::kOffline;
  if (any_missed) {
    out.next_ping_deadline = latest_missed;
  }
  return out;
}
inline std::optional<PeerPresence> AggregatePeerPresence(
    std::vector<ConvertedServerTiming> const& samples) noexcept {
  PeerTimingAggregateContext ctx;
  ctx.expected_server_count = samples.size();
  ctx.success_count = samples.size();
  ctx.successes = samples;
  return AggregatePeerPresence(ctx);
}
// Deprecated alias kept for transitional call sites / older tests.
inline std::optional<PeerReceiveSchedule> AggregatePeerTimings(
    PeerTimingAggregateContext const& ctx) noexcept {
  auto presence = AggregatePeerPresence(ctx);
  if (!presence.has_value()) {
    return std::nullopt;
  }
  PeerReceiveSchedule out{};
  if (presence->last_online.has_value()) {
    out.last_online = *presence->last_online;
  }
  out.next_ping_deadline = presence->next_ping_deadline;
  switch (presence->state) {
    case PeerPresenceState::kOnline:
      out.state = PeerScheduleState::kExpected;
      break;
    case PeerPresenceState::kOffline:
      out.state = PeerScheduleState::kMissedDeadline;
      break;
    case PeerPresenceState::kUnknown:
      out.state = PeerScheduleState::kUnknown;
      break;
  }
  return out;
}
inline std::optional<PeerReceiveSchedule> AggregatePeerTimings(
    std::vector<ConvertedServerTiming> const& samples) noexcept {
  PeerTimingAggregateContext ctx;
  ctx.expected_server_count = samples.size();
  ctx.success_count = samples.size();
  ctx.successes = samples;
  return AggregatePeerTimings(ctx);
}
enum class ServerTimingAttemptStatus {
  kPending,
  kInFlight,
  kRetrying,
  kSuccess,
  kTerminalError,
};
struct ServerTimingAttempt {
  std::uint64_t send_generation{0};
  TimePoint qsend{};
  Duration one_way{};
  ServerTimingAttemptStatus status{ServerTimingAttemptStatus::kPending};
  ConvertedServerTiming converted{};
  ClientTiming raw{};
  bool has_raw{false};
};
struct ServerTimingDiagnostic {
  ServerId server_id{};
  ServerTimingAttemptStatus status{ServerTimingAttemptStatus::kPending};
  ClientTiming raw{};
  bool has_raw{false};
  ConvertedServerTiming converted{};
  TimePoint qsend{};
  Duration one_way{};
};
// Pure helper for unit tests: generation, stale ignore, per-server isolation.
struct PeerTimingQueryState {
  std::uint64_t query_generation{0};
  bool cancelled{false};
  bool completed{false};
  int user_callback_count{0};
  bool snapshot_incomplete{false};
  PeerTimingQueryCoverage coverage{};
  std::vector<ServerId> expected_server_ids;
  std::map<ServerId, ServerTimingAttempt> attempts;
  std::uint64_t Begin(std::vector<ServerId> expected = {},
                      bool incomplete = false,
                      PeerTimingQueryCoverage cov = {}) {
    ++query_generation;
    cancelled = false;
    completed = false;
    user_callback_count = 0;
    snapshot_incomplete = incomplete;
    coverage = cov;
    expected_server_ids = std::move(expected);
    if (coverage.queried_server_count == 0) {
      coverage.queried_server_count = expected_server_ids.size();
    }
    attempts.clear();
    for (auto const id : expected_server_ids) {
      attempts[id].status = ServerTimingAttemptStatus::kPending;
    }
    return query_generation;
  }
  bool IsCurrentQuery(std::uint64_t generation) const noexcept {
    return !cancelled && generation == query_generation;
  }
  std::uint64_t RegisterSend(ServerId server_id, TimePoint qsend,
                             Duration one_way) {
    auto& attempt = attempts[server_id];
    if (attempt.status == ServerTimingAttemptStatus::kSuccess) {
      return attempt.send_generation;
    }
    ++attempt.send_generation;
    attempt.qsend = qsend;
    attempt.one_way = one_way;
    attempt.status = ServerTimingAttemptStatus::kInFlight;
    attempt.converted = {};
    attempt.raw = {};
    attempt.has_raw = false;
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
    if (it->second.status == ServerTimingAttemptStatus::kSuccess) {
      return false;
    }
    it->second.status = ServerTimingAttemptStatus::kSuccess;
    it->second.raw = timing;
    it->second.has_raw = true;
    it->second.converted = ConvertClientTiming(
        it->second.qsend, it->second.one_way, timing, server_id);
    return true;
  }
  bool ApplyTransientError(ServerId server_id, std::uint64_t send_generation) {
    if (cancelled || completed) {
      return false;
    }
    auto it = attempts.find(server_id);
    if (it == attempts.end() || it->second.send_generation != send_generation) {
      return false;
    }
    if (it->second.status == ServerTimingAttemptStatus::kSuccess ||
        it->second.status == ServerTimingAttemptStatus::kTerminalError) {
      return false;
    }
    it->second.status = ServerTimingAttemptStatus::kRetrying;
    return true;
  }
  bool ApplyTerminalError(ServerId server_id, std::uint64_t send_generation) {
    if (cancelled || completed) {
      return false;
    }
    auto it = attempts.find(server_id);
    if (it == attempts.end() || it->second.send_generation != send_generation) {
      return false;
    }
    if (it->second.status == ServerTimingAttemptStatus::kSuccess) {
      return false;
    }
    it->second.status = ServerTimingAttemptStatus::kTerminalError;
    return true;
  }
  bool ApplyError(ServerId server_id, std::uint64_t send_generation) {
    return ApplyTerminalError(server_id, send_generation);
  }
  bool MarkTerminalError(ServerId server_id) {
    if (cancelled || completed) {
      return false;
    }
    auto& attempt = attempts[server_id];
    if (attempt.status == ServerTimingAttemptStatus::kSuccess) {
      return false;
    }
    attempt.status = ServerTimingAttemptStatus::kTerminalError;
    return true;
  }
  bool ReadyToComplete() const {
    if (cancelled || completed) {
      return false;
    }
    auto ids = expected_server_ids;
    if (ids.empty()) {
      ids.reserve(attempts.size());
      for (auto const& [id, _] : attempts) {
        ids.push_back(id);
      }
    }
    if (ids.empty()) {
      return false;
    }
    for (auto const id : ids) {
      auto it = attempts.find(id);
      if (it == attempts.end()) {
        return false;
      }
      auto const status = it->second.status;
      if (status != ServerTimingAttemptStatus::kSuccess &&
          status != ServerTimingAttemptStatus::kTerminalError) {
        return false;
      }
    }
    return true;
  }
  // Presence completes early on the first Expected success.
  bool ReadyToCompletePresence() const {
    if (cancelled || completed) {
      return false;
    }
    for (auto const& [_, attempt] : attempts) {
      if (attempt.status == ServerTimingAttemptStatus::kSuccess &&
          attempt.converted.state == PeerScheduleState::kExpected) {
        return true;
      }
    }
    return ReadyToComplete();
  }
  PeerTimingAggregateContext BuildAggregateContext() const {
    PeerTimingAggregateContext ctx;
    ctx.snapshot_incomplete = snapshot_incomplete;
    auto ids = expected_server_ids;
    if (ids.empty()) {
      ids.reserve(attempts.size());
      for (auto const& [id, _] : attempts) {
        ids.push_back(id);
      }
    }
    ctx.expected_server_count = ids.size();
    ctx.successes.reserve(ids.size());
    for (auto const id : ids) {
      auto it = attempts.find(id);
      if (it == attempts.end()) {
        ++ctx.unresolved_count;
        continue;
      }
      switch (it->second.status) {
        case ServerTimingAttemptStatus::kSuccess:
          ++ctx.success_count;
          ctx.successes.push_back(it->second.converted);
          break;
        case ServerTimingAttemptStatus::kTerminalError:
          ++ctx.terminal_error_count;
          break;
        default:
          ++ctx.unresolved_count;
          break;
      }
    }
    return ctx;
  }
  std::optional<PeerReceiveSchedule> TryAggregate() const {
    return AggregatePeerTimings(BuildAggregateContext());
  }
  std::optional<PeerPresence> TryAggregatePresence() const {
    return AggregatePeerPresence(BuildAggregateContext());
  }
  std::vector<ServerTimingDiagnostic> Diagnostics() const {
    std::vector<ServerTimingDiagnostic> out;
    out.reserve(attempts.size());
    for (auto const& [id, attempt] : attempts) {
      ServerTimingDiagnostic d;
      d.server_id = id;
      d.status = attempt.status;
      d.raw = attempt.raw;
      d.has_raw = attempt.has_raw;
      d.converted = attempt.converted;
      d.qsend = attempt.qsend;
      d.one_way = attempt.one_way;
      out.push_back(d);
    }
    return out;
  }
  PeerTimingQueryCoverage QueryCoverage() const {
    auto c = coverage;
    c.queried_server_count = expected_server_ids.size();
    c.successful_server_count = 0;
    c.failed_server_count = 0;
    for (auto const id : expected_server_ids) {
      auto it = attempts.find(id);
      if (it == attempts.end()) {
        continue;
      }
      if (it->second.status == ServerTimingAttemptStatus::kSuccess) {
        ++c.successful_server_count;
      } else if (it->second.status ==
                 ServerTimingAttemptStatus::kTerminalError) {
        ++c.failed_server_count;
      }
    }
    return c;
  }
  void Cancel() { cancelled = true; }
};
// In-memory presence orchestration without Client/Cloud.
struct PeerPresenceQueryOrchestrator {
  PeerTimingQueryState state;
  int callback_count{0};
  std::optional<PeerPresence> last_presence;
  std::optional<PeerReceiveSchedule> last_schedule;
  std::optional<int> last_error;
  void Start(std::vector<ServerId> ids, bool incomplete = false) {
    state.Begin(std::move(ids), incomplete);
    callback_count = 0;
    last_presence.reset();
    last_schedule.reset();
    last_error.reset();
  }
  std::uint64_t Send(ServerId id, TimePoint qsend, Duration one_way) {
    return state.RegisterSend(id, qsend, one_way);
  }
  void OnSuccess(ServerId id, std::uint64_t gen, ClientTiming const& timing) {
    if (!state.ApplyTiming(id, gen, timing)) {
      return;
    }
    TryFinish();
  }
  void OnTransient(ServerId id, std::uint64_t gen) {
    if (!state.ApplyTransientError(id, gen)) {
      return;
    }
    TryFinish();
  }
  void OnTerminal(ServerId id, std::uint64_t gen) {
    if (!state.ApplyTerminalError(id, gen)) {
      return;
    }
    TryFinish();
  }
  void Destroy() { state.Cancel(); }
  void TryFinish() {
    if (!state.ReadyToCompletePresence() || state.completed) {
      return;
    }
    auto aggregated = state.TryAggregatePresence();
    state.completed = true;
    ++state.user_callback_count;
    ++callback_count;
    if (aggregated.has_value()) {
      last_presence = *aggregated;
      last_schedule = AggregatePeerTimings(state.BuildAggregateContext());
    } else {
      last_error = static_cast<int>(3);
    }
  }
};
// Back-compat alias for older unit tests.
using PeerTimingQueryOrchestrator = PeerPresenceQueryOrchestrator;
enum class QueryPeerReceiveScheduleError : int {
  kGetCloudFailed = 1,
  kNoWorkServerAvailable = 2,
  kGetClientTimingFailed = 3,
  kServerNotInCloud = 4,
};
// SERVER-SCOPED: one peer + one server -> that server's PeerReceiveSchedule.
// MissedDeadline is server-local and must NOT be read as global Offline.
// Does not use observer receive_window / ping_interval for classification.
class QueryPeerReceiveSchedule final : public Action {
 public:
  using ResultEvent = Event<void(Result<PeerReceiveSchedule, int>)>;
  QueryPeerReceiveSchedule(AeContext const& ae_context, Client& client,
                           Uid peer_uid, ServerId server_id);
  ~QueryPeerReceiveSchedule() override;
  AE_CLASS_NO_COPY_MOVE(QueryPeerReceiveSchedule)
  ResultEvent::Subscriber result_event() noexcept;
  std::vector<ServerTimingDiagnostic> const& server_diagnostics() const noexcept;
  PeerTimingQueryCoverage coverage() const noexcept;
  Uid peer_uid() const noexcept { return peer_uid_; }
  ServerId server_id() const noexcept { return server_id_; }
 private:
  Duration OneWayEstimateFor(CloudServerConnection* sc) const;
  void OnCloud(Result<Cloud::ptr, int> result);
  void StartQuery();
  void OnServerTiming(CloudServerConnection* sc, std::uint64_t send_generation,
                      Result<ClientTiming, std::int32_t> const& res);
  void MaybeComplete();
  void Complete(PeerReceiveSchedule const& schedule);
  void Fail(int code);
  AeContext ae_context_;
  Client* client_{nullptr};
  Uid peer_uid_{};
  ServerId server_id_{};
  ResultEvent result_event_;
  Subscription get_cloud_sub_;
  Subscription cloud_request_sub_;
  Subscription exhausted_sub_;
  std::unique_ptr<CloudServerConnections> dest_cloud_;
  CloudServerConnections* work_cloud_{nullptr};
  std::optional<CloudRequest> cloud_request_;
  std::map<ServerId, Subscription> timing_subs_;
  PeerTimingQueryState query_state_{};
  std::vector<ServerTimingDiagnostic> diagnostics_;
  bool finished_{false};
};
}  // namespace ae
#endif  // AETHER_AE_ACTIONS_QUERY_PEER_RECEIVE_SCHEDULE_H_
