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

#include "aether/cloud_connections/ping_schedule_guard.h"

#include <cassert>
#include <type_traits>
#include <variant>

#if AE_ENABLE_PING

#  include "aether/channels/channel.h"
#  include "aether/executors/executors.h"

#  include "aether/cloud_connections/cloud_connections_tele.h"

namespace ae {

PingCloudServers::ServerPing::ServerPing(AeContext const& ae_context,
                                         ClientConnectivityPolicy& policy,
                                         CloudServerConnection& cloud_sc,
                                         std::size_t priority)
    : ae_context_{ae_context},
      policy_{&policy},
      cloud_sc_{&cloud_sc},
      priority_{priority} {
  assert(priority < policy_->rx_timings().size() &&
         "Server ping priority should be in timings range");

  auto const& timings = policy_->rx_timings()[priority_];
  timing_conf_ = timings.conf;

  // if it's to early for next rx wait a bit
  if ((timings.next_rx_point != TimePoint{}) &&
      (Now() < timings.next_rx_point)) {
    AE_TELED_DEBUG("Wait a bit for next rx point till {}",
                   timings.next_rx_point);
    start_sub_ = ae_context_.scheduler().DelayedTask([&]() { Start(); },
                                                     timings.next_rx_point);
  } else {
    // acquire suspend for first ping
    ping_blocker_ = policy_->AcquireSuspendBlock();
    start_sub_ = ae_context_.scheduler().Task([&]() { Start(); });
  }
}

PingCloudServers::ServerPing::~ServerPing() = default;

void PingCloudServers::ServerPing::Stop() {
  stop_ = true;

  waiter_.reset();
  start_sub_.Reset();
  rx_window_sub_.Reset();
  restream_sub_.Reset();
  link_state_sub_.Reset();

  ping_blocker_.Reset();
  rx_window_blocker_.Reset();
  restream_blocker_.Reset();
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

auto PingCloudServers::ServerPing::MakePing() {
  return ex::let_value([&]() noexcept {
    return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
        [&](auto& ctx) noexcept {
          // make ping action with timeout based on response statistics
          // and timing properties for current server RxTimings
          // during the ping and rx window setup suspend blocker
          // and save expected next_ping_time_
          auto* cc = cloud_sc_->client_connection();
          assert(cc != nullptr && "Client connection should exists");

          auto c = cc->server_connection().current_channel();
          if (c == nullptr) {
            AE_TELED_ERROR("Current channel value invalid");
            return ex::set_error(std::move(ctx.receiver), 2);
          }

          // Wire still announces full interval + rx_window; local schedule
          // sends earlier by the response-stats guard.
          auto const& response_stats =
              c->channel_statistics().response_time_statistics();
          auto const guard = ComputePingSendGuardFromStats(
              response_stats, timing_conf_.interval);

          ping_.emplace(ae_context_, *cloud_sc_, timing_conf_.interval,
                        timing_conf_.rx_window, c->ResponseTimeout());

          ping_blocker_ = policy_->AcquireSuspendBlock();
          ping_->result_event().Subscribe(
              [this](Ping::PingResult const& res) noexcept {
                OnPingResult(res);
                ping_blocker_.Reset();
              });

          // run ping request and open rx capability at send
          auto const current_time = Now();
          policy_->ReportPingDispatched(current_time, c->ResponseTimeout());
          ping_->Start(current_time);
          OpenRxWindow();
          if (timing_conf_.interval > Duration{}) {
            next_ping_time_ = current_time + timing_conf_.interval - guard;
            policy_->ReportNextServiceTime(priority_, next_ping_time_);
          } else {
            next_ping_time_ = TimePoint::max();
          }
          AE_TELED_DEBUG(
              "Next ping time for priority {} at {} after {} (guard {})",
              priority_, next_ping_time_, timing_conf_.interval, guard);

          return ex::set_value(std::move(ctx.receiver));
        });
  });
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
          MakePing() |
          // track Stop command
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
        if (res && res->IsOk()) {
          // interval == 0: unknown next ping; do not auto-reschedule.
          if (timing_conf_.interval > Duration{}) {
            start_sub_ = ae_context_.scheduler().DelayedTask(
                [&]() noexcept { Start(); },  // ~['_']~
                next_ping_time_);
          }
        } else if (res && res->IsErr()) {
          AE_TELED_ERROR("Ping start error {}", std::move(res)->error());
        } else {
          AE_TELED_DEBUG("Server ping stopped");
        }
      });
}

void PingCloudServers::ServerPing::OnPingResult(Ping::PingResult const& res) {
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
      [this, c](auto const& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Ok<Duration>>) {
          c->channel_statistics().AddResponseTime(value.value);
          policy_->ReportSuccessfulCloudResponse(Now());
          ScheduleRxWindowClose(
              ComputeRxWindowCloseTime(Now(), timing_conf_.rx_window));
        } else if constexpr (std::is_same_v<T, Ping::LateDuration>) {
          AE_TELED_DEBUG("Got late ping duration");
          c->channel_statistics().AddResponseTime(value.duration);
          policy_->ReportSuccessfulCloudResponse(Now());
          ScheduleRxWindowClose(
              ComputeRxWindowCloseTime(Now(), timing_conf_.rx_window));
        } else if constexpr (std::is_same_v<T, Error<int>>) {
          AE_TELED_ERROR("Ping error!");
          policy_->ReportPingCompletedWithoutSuccess(Now());
          // Error 1 = write failure before/at send: close RX immediately.
          // Other errors (e.g. timeout after write): close at now + window.
          if (value.error == 1) {
            CloseRxWindowNow();
          } else {
            ScheduleRxWindowClose(
                ComputeRxWindowCloseTime(Now(), timing_conf_.rx_window));
          }
          ScheduleRestream();
        } else {
          AE_TELED_ERROR("Ping error!");
          policy_->ReportPingCompletedWithoutSuccess(Now());
          ScheduleRxWindowClose(
              ComputeRxWindowCloseTime(Now(), timing_conf_.rx_window));
          ScheduleRestream();
        }
      },
      res);
}

void PingCloudServers::ServerPing::OpenRxWindow() {
  // Open RX capability at send; do not schedule close at send + window.
  rx_window_blocker_ = policy_->AcquireSuspendBlock();
  rx_window_sub_.Reset();
}

void PingCloudServers::ServerPing::ScheduleRxWindowClose(TimePoint close_time) {
  rx_window_sub_ = ae_context_.scheduler().DelayedTask(
      [this]() { rx_window_blocker_.Reset(); }, close_time);
}

void PingCloudServers::ServerPing::CloseRxWindowNow() {
  rx_window_sub_.Reset();
  rx_window_blocker_.Reset();
}

void PingCloudServers::ServerPing::ScheduleRestream() {
  if (stop_) {
    return;
  }

  // TODO: should we block till restream?
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
}  // namespace ae

#endif
