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
#include <set>
#include <type_traits>
#include <utility>
#include <variant>

#if AE_ENABLE_PING

#  include "aether/channels/channel.h"
#  include "aether/cloud_connections/cloud_connections_tele.h"
#  include "aether/executors/executors.h"
#  include <functional>

namespace ae {

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

  policy_->BindServerPriority(server_id_, priority_);
  auto& presence = policy_->EnsureServerPresence(server_id_);
  policy_->SetServerSelectedForAggregate(server_id_, true);
  machine_.SetDesired(Now(), presence.desired,
                      presence.rtt_reliability_percentile);
  machine_.SetOfflineDetectionTimeout(policy_->offline_detection_timeout());
  if (presence.has_confirmed_schedule) {
    machine_.RestoreConfirmed(
        presence.confirmed_window_open_local,
        presence.confirmed_window_close_local, presence.confirmed_interval,
        presence.confirmed_rx_window, Now(), SelectedRtt());
  } else {
    machine_.ArmInitial(Now());
  }
  // Browser cooperative loop: absolute/long DelayedTask wakes have been
  // unreliable; keep a 1s heartbeat so pull_messages / presence still pump.
  ArmHeartbeat();
  Pump();
}

void PingCloudServers::ServerPing::ArmHeartbeat() {
  if (stop_) {
    return;
  }
  heartbeat_sub_ = ae_context_.scheduler().DelayedTask(
      [this]() noexcept {
        ArmHeartbeat();
        Pump();
      },
      std::chrono::milliseconds{1000});
}

PingCloudServers::ServerPing::~ServerPing() {
  stop_ = true;
  waiter_.reset();
  live_.clear();
  wake_sub_.Reset();
  current_window_sub_.Reset();
  restream_sub_.Reset();
  heartbeat_sub_.Reset();
  link_state_sub_.Reset();
  request_blocker_.Reset();
  current_window_blocker_.Reset();
  restream_blocker_.Reset();
}

void PingCloudServers::ServerPing::PauseForQuarantine() {
  machine_.OnQuarantine(Now());
  waiter_.reset();
  live_.clear();
  policy_->SetServerQuarantined(server_id_, true);
  SyncBlockers();
  Pump();
}

void PingCloudServers::ServerPing::ResumeFromQuarantine() {
  policy_->SetServerQuarantined(server_id_, false);
  machine_.OnQuarantineReleased(Now(), SelectedRtt());
  Pump();
}

void PingCloudServers::ServerPing::NotifyConfigChanged() {
  if (stop_) {
    return;
  }
  auto* presence = policy_->FindServerPresence(server_id_);
  if (presence == nullptr) {
    return;
  }
  machine_.SetDesired(Now(), presence->desired,
                      presence->rtt_reliability_percentile);
  machine_.SetOfflineDetectionTimeout(policy_->offline_detection_timeout());
  Pump();
}

void PingCloudServers::ServerPing::Pump() {
  if (stop_) {
    return;
  }
  auto const now = Now();
  auto const rtt = SelectedRtt();
  // Keep draining even if Ping ApiPromise / hard-wait is stuck.
  if (auto* cc = cloud_sc_->client_connection();
      cc != nullptr &&
      cc->stream_info().link_state == LinkState::kLinked) {
    static_cast<void>(cc->AuthorizedApiCall(
        SubApi{[](ApiContext<AuthorizedApi>& auth_api) {
          auth_api->pull_messages();
        }}));
  }
  auto tick = machine_.TickNow(now, rtt);
  SyncBlockers();
  DropFinishedPings();
  if (tick.restream) {
    ScheduleRestream();
  }
  if (tick.want_send) {
    StartSend(tick.send);
    return;
  }
  ScheduleWake(tick.next_wake);
}

void PingCloudServers::ServerPing::ScheduleWake(TimePoint when) {
  next_wake_ = when;
  policy_->ReportNextServiceTime(priority_, next_wake_);
  if (when == TimePoint::max()) {
    wake_sub_.Reset();
    return;
  }
  auto const now = Now();
  if (when <= now) {
    wake_sub_ = ae_context_.scheduler().Task([this]() noexcept { Pump(); });
    return;
  }
  // Duration-based delay — absolute TimePoint DelayedTask has been unreliable
  // in the Emscripten cooperative loop (presence Pump never woke).
  auto const delay = std::chrono::duration_cast<Duration>(when - now);
  wake_sub_ = ae_context_.scheduler().DelayedTask(
      [this]() noexcept { Pump(); }, delay);
}

void PingCloudServers::ServerPing::SyncBlockers() {
  if (machine_.current_window_blocker_held()) {
    if (!holding_current_) {
      current_window_blocker_ = policy_->AcquireSuspendBlock();
      holding_current_ = true;
    }
    auto const close = machine_.current_promised_close();
    if (scheduled_current_close_ != close) {
      scheduled_current_close_ = close;
      auto const now = Now();
      if (close <= now) {
        current_window_sub_ = ae_context_.scheduler().Task([this]() noexcept {
          holding_current_ = false;
          scheduled_current_close_ = {};
          current_window_blocker_.Reset();
          Pump();
        });
      } else {
        auto const delay = std::chrono::duration_cast<Duration>(close - now);
        current_window_sub_ = ae_context_.scheduler().DelayedTask(
            [this]() noexcept {
              holding_current_ = false;
              scheduled_current_close_ = {};
              current_window_blocker_.Reset();
              Pump();
            },
            delay);
      }
    }
  } else {
    current_window_sub_.Reset();
    current_window_blocker_.Reset();
    holding_current_ = false;
    scheduled_current_close_ = {};
  }

  if (machine_.request_blocker_held()) {
    if (!holding_request_) {
      request_blocker_ = policy_->AcquireSuspendBlock();
      holding_request_ = true;
    }
  } else {
    request_blocker_.Reset();
    holding_request_ = false;
  }
}

void PingCloudServers::ServerPing::DropFinishedPings() {
  for (auto it = live_.begin(); it != live_.end();) {
    bool found = false;
    for (auto const& attempt : machine_.attempts()) {
      if (attempt.attempt_id == it->first) {
        found = true;
        break;
      }
    }
    if (!found) {
      it = live_.erase(it);
    } else {
      ++it;
    }
  }
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

void PingCloudServers::ServerPing::StartSend(
    LocalPresenceMachine::SendSpec spec) {
  if (stop_) {
    return;
  }
  machine_.OnSendStarting();
  SyncBlockers();
  // Capture SendSpec by value: AsyncWaiter / let_value may run after this
  // stack frame returns; [&] to the parameter was UAF (garbage attempt_id /
  // kind / wire) and DropFinishedPings then destroyed the live Ping.
  waiter_.emplace(
      ae_context_,
      EnsureLinked() |
          ex::let_value(
              [this]() noexcept
                  -> ex::variant_sender<decltype(ex::just()),
                                        decltype(ex::just_stopped())> {
                if (stop_) {
                  return ex::just_stopped();
                }
                return ex::just();
              }) |
          ex::let_value([this, spec]() noexcept {
            return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
                [this, spec](auto& ctx) noexcept {
                  auto* cc = cloud_sc_->client_connection();
                  if (cc == nullptr) {
                    return ex::set_error(std::move(ctx.receiver), 1);
                  }
                  auto c = cc->server_connection().current_channel();
                  if (c == nullptr) {
                    AE_TELED_ERROR("Current channel value invalid");
                    return ex::set_error(std::move(ctx.receiver), 2);
                  }

                  auto const send_time = Now();
                  // Separate void-only pull so queue drain cannot be blocked by
                  // ping ApiPromise parse/result issues on browser WSS.
                  static_cast<void>(cc->AuthorizedApiCall(
                      SubApi{[](ApiContext<AuthorizedApi>& auth_api) {
                        auth_api->pull_messages();
                      }}));
                  auto ping = std::make_unique<Ping>(
                      ae_context_, *cloud_sc_, spec.wire_interval,
                      spec.rx_window, spec.hard_wait);
                  auto const attempt_id = spec.attempt_id;
                  ping->result_event().Subscribe(
                      [this, attempt_id](Ping::PingResult const& res) noexcept {
                        OnPingResult(attempt_id, res);
                      });
                  ping->Start(send_time);
                  machine_.OnAttemptSent(spec, send_time);
                  LiveAttempt live{};
                  live.spec = spec;
                  live.send_time = send_time;
                  live.ping = std::move(ping);
                  live_[attempt_id] = std::move(live);
                  AE_TELED_DEBUG(
                      "PING_ATTEMPT server {} id {} kind {} send {} "
                      "wire_us {} hard_us {}",
                      server_id_, attempt_id, static_cast<int>(spec.kind),
                      send_time, spec.wire_interval.count(),
                      spec.hard_wait.count());
                  return ex::set_value(std::move(ctx.receiver));
                });
          }) |
          ex::let_value(
              [this]() noexcept
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
          machine_.OnStartFailed(Now(), SelectedRtt(),
                                 PresenceRestreamReason::kConnectionUnavailable);
          SyncBlockers();
          Pump();
        } else if (!(res && res->IsOk())) {
          AE_TELED_DEBUG("Server ping stopped");
          machine_.OnStartFailed(Now(), SelectedRtt(),
                                 PresenceRestreamReason::kNone);
          SyncBlockers();
        } else {
          Pump();
        }
      });
}

void PingCloudServers::ServerPing::OnPingResult(std::uint64_t attempt_id,
                                                Ping::PingResult const& res) {
  if (stop_) {
    return;
  }
  auto it = live_.find(attempt_id);
  if (it == live_.end()) {
    return;
  }
  auto spec = it->second.spec;
  auto send_time = it->second.send_time;

  std::visit(
      [this, attempt_id, spec, send_time](auto const& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Ok<Duration>> ||
                      std::is_same_v<T, Ping::LateDuration>) {
          Duration measured{};
          if constexpr (std::is_same_v<T, Ok<Duration>>) {
            measured = value.value;
          } else {
            measured = value.duration;
          }
          AddRttSample(measured);
          auto const selected = SelectedRtt();
          auto outcome = machine_.OnPong(
              attempt_id, spec.cycle_id, send_time, Now(), spec.wire_interval,
              spec.desired_interval, spec.rx_window,
              spec.following_open_target, selected);
          ApplyConfirmed(outcome);
          live_.erase(attempt_id);
          SyncBlockers();
          Pump();
        } else {
          auto const code = value.error;
          if (code == 2) {
            machine_.OnHardWaitExpired(attempt_id, Now());
            live_.erase(attempt_id);
            SyncBlockers();
            Pump();
            return;
          }
          auto reason = PresenceRestreamReason::kPingApiError;
          if (code == 1) {
            reason = PresenceRestreamReason::kHardWriteFailure;
          }
          machine_.OnHardFailure(attempt_id, Now(), SelectedRtt(), reason);
          live_.erase(attempt_id);
          SyncBlockers();
          Pump();
        }
      },
      res);
}

void PingCloudServers::ServerPing::AddRttSample(Duration measured) {
  auto* cc = cloud_sc_->client_connection();
  if (cc == nullptr) {
    return;
  }
  auto c = cc->server_connection().current_channel();
  if (!c) {
    return;
  }
  c->channel_statistics().AddResponseTime(measured);
}

void PingCloudServers::ServerPing::ApplyConfirmed(
    LocalPresenceMachine::PongOutcome const& outcome) {
  if (outcome.disposition !=
      LocalPresenceMachine::PongDisposition::kConfirmedSchedule) {
    return;
  }
  if (!machine_.has_confirmed_schedule()) {
    policy_->ConfirmServerPong(server_id_, outcome.schedule.ping_send_time,
                               outcome.schedule.pong_receive_time, Duration{},
                               outcome.schedule.rx_window,
                               outcome.schedule.selected_rtt);
    return;
  }
  policy_->ConfirmServerPong(
      server_id_, outcome.schedule.ping_send_time,
      outcome.schedule.pong_receive_time, outcome.schedule.interval,
      outcome.schedule.rx_window, outcome.schedule.selected_rtt);
  auto& state = policy_->EnsureServerPresence(server_id_);
  state.confirmed_interval = machine_.confirmed_interval();
  state.config_change_pending = machine_.config_change_pending();
  AE_TELED_DEBUG("SCHEDULE confirmed server {} open {} close {}", server_id_,
                 outcome.schedule.window_open_local,
                 outcome.schedule.window_close_local);
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
  RemoveMissingServers();
}

void PingCloudServers::ReconcileServer(CloudServerConnection& cloud_sc) {
  auto const server_id = cloud_sc.server_id();
  auto const priority = cloud_sc.priority();

  auto it = server_pings_.find(server_id);
  if ((it == server_pings_.end()) || (it->second->priority() != priority)) {
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
    it->second->PauseForQuarantine();
  }
}

void PingCloudServers::ServerQuarantineReleased(
    CloudServerConnection* cloud_sc) {
  if (cloud_sc == nullptr) {
    return;
  }
  auto it = server_pings_.find(cloud_sc->server_id());
  if (it != server_pings_.end()) {
    it->second->ResumeFromQuarantine();
  }
}

void PingCloudServers::OnServerRxTimingChanged(ServerId server_id) {
  auto it = server_pings_.find(server_id);
  if (it != server_pings_.end() && !it->second->quarantined()) {
    it->second->NotifyConfigChanged();
  }
}

void PingCloudServers::RemoveMissingServers() {
  std::set<ServerId> in_cloud;
  for (auto* sc : cloud_server_connections_->servers()) {
    if (sc != nullptr) {
      in_cloud.insert(sc->server_id());
    }
  }
  for (auto it = server_pings_.begin(); it != server_pings_.end();) {
    if (in_cloud.find(it->first) == in_cloud.end()) {
      policy_->RemoveServerFromCloud(it->first);
      it = server_pings_.erase(it);
    } else {
      ++it;
    }
  }
}
}  // namespace ae

#endif
