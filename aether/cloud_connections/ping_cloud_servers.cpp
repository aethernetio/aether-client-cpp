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

#include "aether/cloud_connections/ping_cloud_servers.h"

#include <cassert>
#include <chrono>
#include <type_traits>
#include <variant>

#if AE_ENABLE_PING

#  include "aether/channels/channel.h"
#  include "aether/cloud_connections/cloud_connections_tele.h"
#  include "aether/executors/executors.h"

namespace ae {

namespace {

char const* AttemptKindName(PingAttemptKind kind) {
  switch (kind) {
    case PingAttemptKind::kInitial:
      return "INITIAL";
    case PingAttemptKind::kPrefix1:
      return "PREFIX1";
    case PingAttemptKind::kPrefix2:
      return "PREFIX2";
    case PingAttemptKind::kRetry:
      return "RETRY";
    case PingAttemptKind::kRecovery:
      return "RECOVERY";
  }
  return "UNKNOWN";
}

}  // namespace

PingCloudServers::ServerPing::ServerPing(AeContext const& ae_context,
                                         ClientConnectivityPolicy& policy,
                                         CloudServerConnection& cloud_sc,
                                         std::size_t priority)
    : ae_context_{ae_context},
      policy_{&policy},
      cloud_sc_{&cloud_sc},
      server_id_{cloud_sc.server_id()},
      priority_{priority} {
  assert(priority < policy_->rx_timings().size() &&
         "Server ping priority should be in timings range");

  auto& presence = policy_->EnsureServerPresence(server_id_);
  if (!presence.has_user_rx_timing) {
    presence.desired = policy_->rx_timings()[priority_].conf;
  }
  active_conf_ = presence.desired;
  policy_->SetServerSelectedForAggregate(server_id_, true);

  ping_blocker_ = policy_->AcquireSuspendBlock();
  start_sub_ = ae_context_.scheduler().Task(
      [this]() { StartAttempt(PingAttemptKind::kInitial); });
}

PingCloudServers::ServerPing::~ServerPing() = default;

void PingCloudServers::ServerPing::Stop() {
  stop_ = true;
  AbandonInFlight();
  waiter_.reset();
  start_sub_.Reset();
  attempt_timeout_sub_.Reset();
  rx_window_sub_.Reset();
  restream_sub_.Reset();
  link_state_sub_.Reset();
  ping_blocker_.Reset();
  rx_window_blocker_.Reset();
  restream_blocker_.Reset();
  policy_->SetServerSelectedForAggregate(server_id_, false);
  policy_->InvalidateConfirmedSchedule(server_id_);
  policy_->RefreshOnlineFlags(Now());
}

void PingCloudServers::ServerPing::NotifyConfigChanged() {
  if (stop_) {
    return;
  }
  auto* presence = policy_->FindServerPresence(server_id_);
  if (presence == nullptr) {
    return;
  }
  active_conf_ = presence->desired;
  if (attempt_in_flight_) {
    return;
  }
  if (presence->config_change_pending || !presence->has_confirmed_schedule) {
    ScheduleNext(Now(), PingAttemptKind::kInitial);
    return;
  }
  auto const rtt = SelectedRtt();
  auto const prefix1 =
      ComputePrefix1Time(presence->confirmed_window_open_local, rtt);
  ScheduleNext(prefix1 > Now() ? prefix1 : Now(), PingAttemptKind::kPrefix1);
}

void PingCloudServers::ServerPing::ScheduleNext(TimePoint when,
                                                PingAttemptKind kind) {
  if (stop_) {
    return;
  }
  next_ping_time_ = when;
  policy_->ReportNextServiceTime(priority_, next_ping_time_);
  auto* presence = policy_->FindServerPresence(server_id_);
  if (presence != nullptr) {
    presence->current_attempt_kind = kind;
  }
  start_sub_ = ae_context_.scheduler().DelayedTask(
      [this, kind]() noexcept { StartAttempt(kind); }, when);
}

void PingCloudServers::ServerPing::StartAttempt(PingAttemptKind kind) {
  if (stop_ || attempt_in_flight_) {
    return;
  }
  auto& presence = policy_->EnsureServerPresence(server_id_);
  active_conf_ = presence.desired;
  presence.current_attempt_kind = kind;
  ++presence.current_attempt_id;
  active_attempt_id_ = presence.current_attempt_id;
  active_attempt_kind_ = kind;
  Start();
}

template <typename F>
void PingCloudServers::ServerPing::WaitForLink(ClientServerConnection& cc,
                                               F&& f) {
  link_state_sub_ = cc.stream_update_event().Subscribe(
      [this, f_ = std::forward<F>(f)]() noexcept {
        auto* cc = cloud_sc_->client_connection();
        if (cc->stream_info().link_state == LinkState::kLinked) {
          link_state_sub_.Reset();
          std::invoke(f_);
        }
      });
}

auto PingCloudServers::ServerPing::EnsureLinked() {
  return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
      [&](auto& ctx) noexcept {
        auto* cc = cloud_sc_->client_connection();
        if (cc == nullptr) {
          return ex::set_error(std::move(ctx.receiver), 1);
        }
        if (cc->stream_info().link_state == LinkState::kLinked) {
          return ex::set_value(std::move(ctx.receiver));
        }
        WaitForLink(*cc, [&]() noexcept {
          return ex::set_value(std::move(ctx.receiver));
        });
      });
}

Duration PingCloudServers::ServerPing::SelectedRtt() const {
  auto* cc = cloud_sc_->client_connection();
  if (cc == nullptr) {
    return std::chrono::milliseconds{AE_DEFAULT_RESPONSE_TIMEOUT_MS};
  }
  auto c = cc->server_connection().current_channel();
  if (!c) {
    return std::chrono::milliseconds{AE_DEFAULT_RESPONSE_TIMEOUT_MS};
  }
  auto const& stats = c->channel_statistics().response_time_statistics();
  auto const* presence = policy_->FindServerPresence(server_id_);
  auto const pct = presence == nullptr ? kDefaultRttReliabilityPercentile
                                       : presence->rtt_reliability_percentile;
  if (stats.empty()) {
    return std::chrono::milliseconds{AE_DEFAULT_RESPONSE_TIMEOUT_MS};
  }
  return stats.PercentileValue(pct);
}

Duration PingCloudServers::ServerPing::AttemptTimeout(Duration rtt) const {
  // Scheduler treats selected RTT as the attempt window; floor with guard.
  if (rtt <= Duration{}) {
    return kLocalPresenceGuard;
  }
  return rtt;
}

void PingCloudServers::ServerPing::AbandonInFlight() {
  attempt_timeout_sub_.Reset();
  if (ping_) {
    ping_.reset();
  }
  attempt_in_flight_ = false;
  ping_blocker_.Reset();
  ++active_attempt_id_;
  auto* presence = policy_->FindServerPresence(server_id_);
  if (presence != nullptr) {
    presence->current_attempt_id = active_attempt_id_;
  }
}

void PingCloudServers::ServerPing::Start() {
  if (stop_) {
    return;
  }
  waiter_.emplace(
      ae_context_,
      EnsureLinked() |
          ex::let_value(
              [&]() noexcept
                  -> ex::variant_sender<decltype(ex::just()),
                                        decltype(ex::just_stopped())> {
                if (stop_) {
                  return ex::just_stopped();
                }
                return ex::just();
              }) |
          ex::let_value([&]() noexcept {
            return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
                [&](auto& ctx) noexcept {
                  auto* cc = cloud_sc_->client_connection();
                  assert(cc != nullptr && "Client connection should exists");
                  auto c = cc->server_connection().current_channel();
                  if (c == nullptr) {
                    AE_TELED_ERROR("Current channel value invalid");
                    return ex::set_error(std::move(ctx.receiver), 2);
                  }

                  auto const rtt = SelectedRtt();
                  auto const timeout = AttemptTimeout(rtt);
                  auto& presence = policy_->EnsureServerPresence(server_id_);
                  active_conf_ = presence.desired;
                  auto const percentile = presence.rtt_reliability_percentile;

                  active_sent_interval_ = active_conf_.interval;
                  active_sent_window_ = active_conf_.rx_window;
                  active_send_time_ = Now();
                  attempt_in_flight_ = true;

                  ping_.emplace(ae_context_, *cloud_sc_, active_sent_interval_,
                                active_sent_window_, timeout);

                  ping_blocker_ = policy_->AcquireSuspendBlock();
                  auto const attempt_id = active_attempt_id_;
                  auto const send_time = active_send_time_;
                  auto const sent_interval = active_sent_interval_;
                  auto const sent_window = active_sent_window_;
                  ping_->result_event().Subscribe(
                      [this, attempt_id, send_time, sent_interval,
                       sent_window](Ping::PingResult const& res) noexcept {
                        OnPingResult(attempt_id, send_time, sent_interval,
                                     sent_window, res);
                      });

                  AE_TELED_DEBUG(
                      "PING_ATTEMPT server {} id {} kind {} send {}",
                      server_id_, attempt_id,
                      AttemptKindName(active_attempt_kind_), send_time);
                  AE_TELED_DEBUG(
                      "RTT server {} percentile {} selected_rtt {}",
                      server_id_, static_cast<unsigned>(percentile), rtt);

                  ping_->Start(send_time);

                  // Scheduler failure of this attempt at selected RTT — not
                  // OFFLINE.
                  attempt_timeout_sub_ = ae_context_.scheduler().DelayedTask(
                      [this, attempt_id]() noexcept {
                        OnAttemptTimeout(attempt_id);
                      },
                      send_time + timeout);

                  return ex::set_value(std::move(ctx.receiver));
                });
          }) |
          ex::let_value(
              [&]() noexcept
                  -> ex::variant_sender<decltype(ex::just()),
                                        decltype(ex::just_stopped())> {
                if (stop_) {
                  return ex::just_stopped();
                }
                return ex::just();
              }),
      [this]<typename R>(std::optional<R>&& res) noexcept {
        if (res && res->IsErr()) {
          AE_TELED_ERROR("Ping start error {}", std::move(res)->error());
          attempt_in_flight_ = false;
          ScheduleRestream();
          ScheduleNext(Now() + SelectedRtt(), PingAttemptKind::kRecovery);
        } else if (!(res && res->IsOk())) {
          AE_TELED_DEBUG("Server ping stopped");
        }
      });
}

void PingCloudServers::ServerPing::OnPingResult(std::uint64_t attempt_id,
                                                TimePoint send_time,
                                                Duration sent_interval,
                                                Duration sent_window,
                                                Ping::PingResult const& res) {
  if (stop_) {
    return;
  }
  // Late / abandoned attempts must not roll confirmed schedule backwards.
  if (!IsCurrentPingAttempt(active_attempt_id_, attempt_id)) {
    AE_TELED_DEBUG("Ignoring stale ping result attempt {}", attempt_id);
    return;
  }

  auto* cc = cloud_sc_->client_connection();
  if (cc == nullptr) {
    AE_TELED_ERROR("Client connection is null");
    return;
  }
  auto c = cc->server_connection().current_channel();
  if (!c) {
    AE_TELED_ERROR("Connection channel is null");
    return;
  }

  std::visit(
      [this, c, send_time, sent_interval, sent_window](auto const& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Ok<Duration>>) {
          c->channel_statistics().AddResponseTime(value.value);
          ApplyConfirmedPong(send_time, Now(), sent_interval, sent_window);
        } else if constexpr (std::is_same_v<T, Ping::LateDuration>) {
          AE_TELED_DEBUG("Got late ping duration for active attempt");
          c->channel_statistics().AddResponseTime(value.duration);
          ApplyConfirmedPong(send_time, Now(), sent_interval, sent_window);
        } else {
          AE_TELED_ERROR("Ping error!");
          AbandonInFlight();
          ScheduleRestream();
          AfterFailedAttempt();
        }
      },
      res);
}

void PingCloudServers::ServerPing::ApplyConfirmedPong(TimePoint send_time,
                                                     TimePoint pong_time,
                                                     Duration sent_interval,
                                                     Duration sent_window) {
  attempt_timeout_sub_.Reset();
  attempt_in_flight_ = false;
  ping_blocker_.Reset();

  policy_->ConfirmServerPong(server_id_, send_time, pong_time, sent_interval,
                             sent_window);
  auto* presence = policy_->FindServerPresence(server_id_);
  assert(presence != nullptr);
  AE_TELED_DEBUG(
      "SCHEDULE confirmed server {} open {} close {} interval {} window {}",
      server_id_, presence->confirmed_window_open_local,
      presence->confirmed_window_close_local, sent_interval, sent_window);
  AE_TELED_DEBUG("STATUS server {} ONLINE", server_id_);

  // Contractual window is a minimum listen guarantee, not a transport close.
  HoldRxUntil(presence->confirmed_window_close_local);

  auto const now = Now();
  auto const rtt = SelectedRtt();
  auto const plan = PlanAfterSuccessfulPong(
      presence->confirmed_window_open_local, now, rtt,
      presence->config_change_pending);
  ScheduleNext(plan.when, plan.kind);
}

void PingCloudServers::ServerPing::OnAttemptTimeout(std::uint64_t attempt_id) {
  if (stop_ || !IsCurrentPingAttempt(active_attempt_id_, attempt_id) ||
      !attempt_in_flight_) {
    return;
  }
  AE_TELED_DEBUG("Ping attempt {} kind {} timed out for scheduler", attempt_id,
                 AttemptKindName(active_attempt_kind_));
  AbandonInFlight();
  AfterFailedAttempt();
}

void PingCloudServers::ServerPing::AfterFailedAttempt() {
  if (stop_) {
    return;
  }
  auto now = Now();
  policy_->RefreshOnlineFlags(now);
  auto* presence = policy_->FindServerPresence(server_id_);
  if (presence == nullptr) {
    ScheduleNext(now + SelectedRtt(), PingAttemptKind::kRecovery);
    return;
  }

  auto const rtt = SelectedRtt();
  if (presence->config_change_pending && presence->has_confirmed_schedule) {
    ScheduleNext(now, PingAttemptKind::kInitial);
    return;
  }

  auto const plan = PlanAfterFailedAttempt(
      presence->has_confirmed_schedule, presence->confirmed_window_open_local,
      presence->confirmed_window_close_local, active_attempt_kind_, now, rtt);
  if (plan.mark_offline) {
    policy_->MarkServerOffline(server_id_, now);
    AE_TELED_DEBUG("STATUS server {} OFFLINE after window close {}", server_id_,
                   presence->confirmed_window_close_local);
  }
  ScheduleNext(plan.when, plan.kind);
}

void PingCloudServers::ServerPing::HoldRxUntil(TimePoint until) {
  // rx_window is a minimum contractual guarantee. Do not close transport.
  rx_window_blocker_ = policy_->AcquireSuspendBlock();
  rx_window_sub_ = ae_context_.scheduler().DelayedTask(
      [this]() { rx_window_blocker_.Reset(); }, until);
}

void PingCloudServers::ServerPing::ScheduleRestream() {
  if (stop_) {
    return;
  }
  restream_blocker_ = policy_->AcquireSuspendBlock();
  restream_sub_ = ae_context_.scheduler().Task([this]() {
    auto* cc = cloud_sc_->client_connection();
    if (cc != nullptr) {
      cc->Restream();
    }
    restream_blocker_.Reset();
  });
}

PingCloudServers::PingCloudServers(
    AeContext const& ae_context,
    CloudServerConnections& cloud_server_connections,
    ClientConnectivityPolicy& policy)
    : ae_context_{ae_context},
      cloud_server_connections_{&cloud_server_connections},
      policy_{&policy},
      servers_update_{
          cloud_server_connections_->servers_update_event().Subscribe(
              MethodPtr<&PingCloudServers::ServersUpdate>{this})} {
  AE_TELED_INFO("PingCloudServers created");
  server_quarantined_sub_ =
      cloud_server_connections_->server_quarantined_event().Subscribe(
          MethodPtr<&PingCloudServers::ServerQuarantined>{this});
  server_quarantine_released_sub_ =
      cloud_server_connections_->server_quarantine_release_event().Subscribe(
          MethodPtr<&PingCloudServers::ServerQuarantineReleased>{this});
  server_rx_timing_changed_sub_ =
      policy_->server_rx_timing_changed_event().Subscribe(
          MethodPtr<&PingCloudServers::OnServerRxTimingChanged>{this});
  ServersUpdate();
}

PingCloudServers::~PingCloudServers() { task_sub_.Reset(); }

void PingCloudServers::ServersUpdate() {
  AE_TELED_DEBUG("Servers update");
  if (!task_sub_) {
    auto blocker = policy_->AcquireSuspendBlock();
    task_sub_ = ae_context_.scheduler().Task(
        [this, blocker = std::move(blocker)]() mutable {
          task_sub_.Reset();
          DispatchToServers();
        });
  }
}

void PingCloudServers::DispatchToServers() {
  cloud_server_connections_->ForServers(
      [this](CloudServerConnection* cloud_sc) {
        if (cloud_sc == nullptr) {
          AE_TELED_ERROR("Visit empty cloud server connection!");
          return;
        }
        auto const& server = cloud_sc->server();
        if (server) {
          ReconcileServer(*cloud_sc);
        }
      },
      policy_->rx_targets());
}

void PingCloudServers::ReconcileServer(CloudServerConnection& cloud_sc) {
  auto const server_id = cloud_sc.server_id();
  auto const priority = cloud_sc.priority();

  auto it = server_pings_.find(server_id);
  if ((it == server_pings_.end()) || (it->second->priority() != priority) ||
      it->second->stopped()) {
    if (it != server_pings_.end()) {
      it->second.reset();
    }
    server_pings_.insert_or_assign(
        server_id, std::make_unique<ServerPing>(ae_context_, *policy_, cloud_sc,
                                                priority));
    return;
  }
  it->second->NotifyConfigChanged();
}

void PingCloudServers::ServerQuarantined(CloudServerConnection* cloud_sc) {
  if (cloud_sc == nullptr) {
    return;
  }
  auto it = server_pings_.find(cloud_sc->server_id());
  if (it != server_pings_.end()) {
    it->second->Stop();
  }
}

void PingCloudServers::ServerQuarantineReleased(
    CloudServerConnection* cloud_sc) {
  if (cloud_sc == nullptr) {
    return;
  }
  auto it = server_pings_.find(cloud_sc->server_id());
  if (it != server_pings_.end()) {
    server_pings_.erase(it);
  }
}

void PingCloudServers::OnServerRxTimingChanged(ServerId server_id) {
  auto it = server_pings_.find(server_id);
  if (it != server_pings_.end() && !it->second->stopped()) {
    it->second->NotifyConfigChanged();
  }
}
}  // namespace ae

#endif
