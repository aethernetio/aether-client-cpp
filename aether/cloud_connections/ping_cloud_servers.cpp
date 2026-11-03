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
#  include "aether/diagnostics/bench_connection_event_diag.h"
#  include "aether/diagnostics/bench_ping_min_timeout.h"

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

PingCloudServers::ServerPing::~ServerPing() { Stop(); }

void PingCloudServers::ServerPing::Stop() {
  stop_ = true;
  ++ping_epoch_;
  ping_wait_connection_ = nullptr;
  ping_wait_channel_ = nullptr;

  bench_ping_server_start::ClearDeferredStart(defer_state_);
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
            AE_TELED_ERROR("Current channel value invalid");
            return ex::set_error(std::move(ctx.receiver), 2);
          }

          Duration const channel_timeout = c->ResponseTimeout();
          Duration const applied_timeout =
              bench_ping_min_timeout::ApplyBenchPingMinTimeout(channel_timeout);
          ++ping_epoch_;
          ping_wait_connection_ = cc;
          ping_wait_channel_ = c.get();
          ping_.emplace(ae_context_, *cloud_sc_, timing_conf_.interval,
                        timing_conf_.rx_window, applied_timeout,
                        channel_timeout);

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
          AE_TELED_DEBUG("Next ping time for priority {} at {} after {}",
                         priority_, next_ping_time_, timing_conf_.interval);

          return ex::set_value(std::move(ctx.receiver));
        });
  });
}

void PingCloudServers::ServerPing::Start() {
  if (stop_) {
    return;
  }
  if (bench_ping_server_start::TryDeferPeriodicStart(
          defer_state_, ping_.has_value(), ping_ && ping_->IsPending(),
          ping_ && ping_->IsAwaitingLateResponse())) {
    AE_TELED_DEBUG("Defer periodic Start while ping pending or awaiting late");
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
          // repeat start on next_ping_time_
          start_sub_ = ae_context_.scheduler().DelayedTask(
              [&]() noexcept { Start(); },  // ~['_']~
              next_ping_time_);
        } else if (res && res->IsErr()) {
          AE_TELED_ERROR("Ping start error {}", std::move(res)->error());
        } else {
          AE_TELED_DEBUG("Server ping stopped");
        }
      });
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
  std::uint64_t const cloud_id = cloud_sc_->bench_cloud_instance_id();
#if !AE_BENCH_CONNECTION_EVENT_DIAG
  (void)cloud_id;
#endif
  std::uint64_t client_id = 0;
  auto* cc = cloud_sc_->client_connection();
  if (cc != nullptr) {
    client_id = cc->bench_client_instance_id();
  }
  AE_BENCH_CONN_DIAG("ping_on_result", cloud_sc_->server_id(), cloud_id,
                     client_id, 0, 0, 0, -1, 0, -1);
  std::uint32_t const epoch = ping_epoch_;

  if (auto const* err = std::get_if<Error<int>>(&res)) {
    if (err->error == 2) {
      bench_ping_server_start::ClearDeferredStart(defer_state_);
      SchedulePingDeferred(epoch, [this, epoch]() {
        HandlePingTimeoutOnScheduler(epoch);
      });
      return;
    }
    AE_TELED_ERROR("Ping error!");
    bench_ping_server_start::ClearDeferredStart(defer_state_);
    AE_BENCH_CONN_DIAG("ping_on_result_restream", cloud_sc_->server_id(), cloud_id,
                       client_id, 0, 0, 0, -1, 0, -1);
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
      if (bench_ping_server_start::TakeDeferredStartAfterSuccess(
              defer_state_)) {
        ScheduleNextPeriodicStart();
      }
    });
    return;
  }

  if (auto const* late = std::get_if<Ping::LateDuration>(&res)) {
    AE_TELED_DEBUG("Got late ping duration");
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

  std::uint64_t const cloud_id = cloud_sc_->bench_cloud_instance_id();
  std::uint64_t client_id = 0;
  if (auto* cc = cloud_sc_->client_connection()) {
    client_id = cc->bench_client_instance_id();
  }
  AE_BENCH_CONN_DIAG("schedule_restream", cloud_sc_->server_id(), cloud_id,
                     client_id, 0, 0, 0, -1, 0, -1);
#if !AE_BENCH_CONNECTION_EVENT_DIAG
  (void)cloud_id;
  (void)client_id;
#endif

  // TODO: should we block till restream?
  restream_blocker_ = policy_->AcquireSuspendBlock();
  restream_sub_ = ae_context_.scheduler().Task([this]() {
    std::uint64_t const cloud_id = cloud_sc_->bench_cloud_instance_id();
    std::uint64_t client_id = 0;
    auto* cc = cloud_sc_->client_connection();
    if (cc != nullptr) {
      client_id = cc->bench_client_instance_id();
    }
    AE_BENCH_CONN_DIAG("schedule_restream_run", cloud_sc_->server_id(),
                       cloud_id, client_id, 0, 0, 0, -1, 0, -1);
#if !AE_BENCH_CONNECTION_EVENT_DIAG
    (void)cloud_id;
    (void)client_id;
#endif
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

void PingCloudServersTestAccess::TriggerPeriodicStart(PingCloudServers& pcs,
                                                      ServerId server_id) {
  auto it = pcs.server_pings_.find(server_id);
  if (it == pcs.server_pings_.end()) {
    return;
  }
  it->second->Start();
}

bool PingCloudServersTestAccess::HasPendingClientPing(PingCloudServers& pcs,
                                                      ServerId server_id) {
  auto it = pcs.server_pings_.find(server_id);
  if (it == pcs.server_pings_.end()) {
    return false;
  }
  auto& sp = *it->second;
  return sp.ping_.has_value() && sp.ping_->IsPending();
}

bool PingCloudServersTestAccess::HasAwaitingLateClientPing(
    PingCloudServers& pcs, ServerId server_id) {
  auto it = pcs.server_pings_.find(server_id);
  if (it == pcs.server_pings_.end()) {
    return false;
  }
  return it->second->AwaitingLatePing();
}

Ping const* PingCloudServersTestAccess::ClientPingPtr(PingCloudServers& pcs,
                                                      ServerId server_id) {
  auto it = pcs.server_pings_.find(server_id);
  if (it == pcs.server_pings_.end() || !it->second->ping_.has_value()) {
    return nullptr;
  }
  return &*it->second->ping_;
}

void PingCloudServersTestAccess::StopServerPing(PingCloudServers& pcs,
                                                ServerId server_id) {
  auto it = pcs.server_pings_.find(server_id);
  if (it == pcs.server_pings_.end()) {
    return;
  }
  it->second->Stop();
}
}  // namespace ae

#endif
