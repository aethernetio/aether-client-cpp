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
#include <type_traits>
#include <variant>

#if AE_ENABLE_PING

#  include "aether/channels/channel.h"
#  include "aether/executors/executors.h"

#  include "aether/cloud_connections/cloud_connections_tele.h"

namespace ae {

PingCloudServers::ServerPing::ServerPing(
    AeContext const& ae_context, ClientConnectivityPolicy& policy,
    CloudServerConnections& cloud_server_connections,
    CloudServerConnection& cloud_sc, std::size_t priority)
    : ae_context_{ae_context},
      policy_{&policy},
      cloud_server_connections_{&cloud_server_connections},
      cloud_sc_{&cloud_sc},
      priority_{priority} {
  assert(priority < policy_->rx_timings().size() &&
         "Server ping priority should be in timings range");

  auto const& timings = policy_->rx_timings()[priority_];
  timing_conf_ = timings.conf;

  // if it's to early for next rx wait a bit
  if ((timings.next_rx_point != TimePoint{}) &&
      (Now() < timings.next_rx_point)) {
    AETHER_TELED_DEBUG("Wait a bit for next rx point till {}",
                   timings.next_rx_point);
    start_sub_ = ae_context_.scheduler().DelayedTask([&]() { Start(); },
                                                     timings.next_rx_point);
  } else {
    // acquire suspend for first ping
    ping_blocker_ = policy_->AcquireSuspendBlock();
    start_sub_ = ae_context_.scheduler().Task([&]() { Start(); });
  }
}

PingCloudServers::ServerPing::~ServerPing() { Stop(); }

void PingCloudServers::ServerPing::Stop() {
  stop_ = true;
  ++ping_epoch_;
  ping_wait_connection_ = nullptr;
  ping_wait_channel_ = nullptr;

  deferred_start_after_ping_ = false;
  waiter_.reset();
  start_sub_.Reset();
  rx_window_sub_.Reset();
  restream_sub_.Reset();
  link_state_sub_.Reset();
  ping_deferred_sub_.Reset();
  ping_finish_sub_.Reset();

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
            AETHER_TELED_ERROR("Current channel value invalid");
            return ex::set_error(std::move(ctx.receiver), 2);
          }

          ++ping_epoch_;
          ping_wait_connection_ = cc;
          ping_wait_channel_ = c.get();
          ping_.emplace(ae_context_, *cloud_sc_, timing_conf_.interval,
                        timing_conf_.rx_window, c->ResponseTimeout());

          ping_blocker_ = policy_->AcquireSuspendBlock();
          ping_->result_event().Subscribe(
              [this](Ping::PingResult const& res) noexcept {
                OnPingResult(res);
                ping_blocker_.Reset();
              });

          // run ping request and open rx window
          auto const current_time = Now();
          ping_->Start(current_time);
          OpenRxWindow(current_time);
          next_ping_time_ = current_time + timing_conf_.interval;
          policy_->ReportNextServiceTime(priority_, next_ping_time_);
          AETHER_TELED_DEBUG("Next ping time for priority {} at {} after {}",
                         priority_, next_ping_time_, timing_conf_.interval);

          return ex::set_value(std::move(ctx.receiver));
        });
  });
}

void PingCloudServers::ServerPing::Start() {
  if (stop_) {
    return;
  }
  if (ping_ && (ping_->IsPending() || ping_->IsAwaitingLateResponse())) {
    deferred_start_after_ping_ = true;
    return;
  }
  if (!IsCurrentRxTarget()) {
    Stop();
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
                if (!IsCurrentRxTarget()) {
                  // Keep the active waiter alive through its completion.
                  stop_ = true;
                  ping_blocker_.Reset();
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
          // repeat start on next_ping_time_
          start_sub_ = ae_context_.scheduler().DelayedTask(
              [&]() noexcept { Start(); },  // ~['_']~
              next_ping_time_);
        } else if (res && res->IsErr()) {
          AETHER_TELED_ERROR("Ping start error {}", std::move(res)->error());
        } else {
          AETHER_TELED_DEBUG("Server ping stopped");
        }
      });
}

bool PingCloudServers::ServerPing::IsCurrentRxTarget() const {
  auto const priority = cloud_sc_->priority();
  if (priority != priority_ || priority >= policy_->rx_timings().size()) {
    return false;
  }
  bool selected = false;
  cloud_server_connections_->ForServers(
      [this, &selected](CloudServerConnection* cloud_sc) {
        selected = selected || cloud_sc == cloud_sc_;
      },
      policy_->rx_targets());
  return selected;
}

bool PingCloudServers::ServerPing::AwaitingLatePing() const noexcept {
  return ping_.has_value() && ping_->IsAwaitingLateResponse();
}

void PingCloudServers::ServerPing::ScheduleNextPeriodicStart() {
  if (stop_) {
    return;
  }
  start_sub_ = ae_context_.scheduler().DelayedTask(
      [&]() noexcept { Start(); }, next_ping_time_);
}

bool PingCloudServers::ServerPing::PingWaitBindingMatches(
    std::uint32_t epoch) const {
  if (stop_ || epoch != ping_epoch_) {
    return false;
  }
  auto* cc = cloud_sc_->client_connection();
  return cc != nullptr && cc == ping_wait_connection_;
}

void PingCloudServers::ServerPing::HandlePingTimeoutOnScheduler(
    std::uint32_t epoch) {
  if (!PingWaitBindingMatches(epoch) || cloud_sc_ == nullptr ||
      cloud_server_connections_ == nullptr) {
    return;
  }
  if (cloud_server_connections_->QuarantineForPingTimeout(*cloud_sc_)) {
    cloud_server_connections_->ScheduleReconcileServers();
  }
}

void PingCloudServers::ServerPing::HandleLatePingOnScheduler(
    Ping::LateDuration const& late, std::uint32_t epoch) {
  if (!PingWaitBindingMatches(epoch) || cloud_sc_ == nullptr ||
      cloud_server_connections_ == nullptr) {
    return;
  }
  if (cloud_sc_->quarantine_reason() !=
      CloudQuarantineReason::kPingTimeoutWait) {
    return;
  }
  auto* cc = cloud_sc_->client_connection();
  auto c = cc->server_connection().current_channel();
  if (!c || c.get() != ping_wait_channel_) {
    return;
  }
  c->channel_statistics().AddResponseTime(late.duration);
  if (cloud_server_connections_->RecoverFromPingTimeout(*cloud_sc_)) {
    SchedulePingFinish(epoch, [this]() {
      ping_.reset();
      ping_wait_connection_ = nullptr;
      ping_wait_channel_ = nullptr;
      ScheduleNextPeriodicStart();
    });
  }
}

void PingCloudServers::ServerPing::OnPingResult(Ping::PingResult const& res) {
  std::uint32_t const epoch = ping_epoch_;

  if (auto const* err = std::get_if<Error<int>>(&res)) {
    if (err->error == 2) {
      deferred_start_after_ping_ = false;
      SchedulePingDeferred(epoch, [this, epoch]() {
        HandlePingTimeoutOnScheduler(epoch);
      });
      return;
    }
    AETHER_TELED_ERROR("Ping error!");
    deferred_start_after_ping_ = false;
    SchedulePingFinish(epoch, [this]() {
      ping_.reset();
      ping_wait_connection_ = nullptr;
      ping_wait_channel_ = nullptr;
      ScheduleRestream();
    });
    return;
  }

  if (auto const* ok = std::get_if<Ok<Duration>>(&res)) {
    Duration const duration = ok->value;
    SchedulePingFinish(epoch, [this, duration]() {
      auto* cc = cloud_sc_->client_connection();
      if (cc != nullptr) {
        auto c = cc->server_connection().current_channel();
        if (c && c.get() == ping_wait_channel_) {
          c->channel_statistics().AddResponseTime(duration);
        }
      }
      ping_.reset();
      ping_wait_connection_ = nullptr;
      ping_wait_channel_ = nullptr;
      if (std::exchange(deferred_start_after_ping_, false)) {
        ScheduleNextPeriodicStart();
      }
    });
    return;
  }

  if (auto const* late = std::get_if<Ping::LateDuration>(&res)) {
    AETHER_TELED_DEBUG("Got late ping duration");
    Ping::LateDuration const late_copy = *late;
    SchedulePingDeferred(epoch, [this, late_copy, epoch]() {
      HandleLatePingOnScheduler(late_copy, epoch);
    });
  }
}

void PingCloudServers::ServerPing::OpenRxWindow(TimePoint sent_time) {
  // keep rx window suspend block for timing_.rx_window time
  rx_window_blocker_ = policy_->AcquireSuspendBlock();
  rx_window_sub_ = ae_context_.scheduler().DelayedTask(
      [this]() { rx_window_blocker_.Reset(); },
      sent_time + timing_conf_.rx_window);
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
  AETHER_TELED_INFO("PingCloudServers created");
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
  AETHER_TELED_DEBUG("Servers update");
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
          AETHER_TELED_ERROR("Visit empty cloud server connection!");
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
    if (it != server_pings_.end() && it->second->AwaitingLatePing()) {
      return;
    }
    if (it != server_pings_.end()) {
      it->second.reset();
    }
    server_pings_.insert_or_assign(
        server_id,
        std::make_unique<ServerPing>(ae_context_, *policy_,
                                     *cloud_server_connections_, cloud_sc,
                                     priority));
    return;
  }
}

void PingCloudServers::ServerQuarantined(CloudServerConnection* cloud_sc) {
  if (cloud_sc == nullptr) {
    return;
  }
  if (cloud_sc->quarantine_reason() ==
      CloudQuarantineReason::kPingTimeoutWait) {
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
