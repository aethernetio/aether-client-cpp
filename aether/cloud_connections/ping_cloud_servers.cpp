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
#include <cstdint>
#include <type_traits>
#include <variant>

#if AE_ENABLE_PING

#  include "aether/channels/channel.h"
#  include "aether/executors/executors.h"

#  include "aether/cloud_connections/cloud_connections_tele.h"

namespace ae {
namespace {
PingTraceHook g_ping_trace_hook{nullptr};
}  // namespace

void SetPingTraceHook(PingTraceHook hook) noexcept { g_ping_trace_hook = hook; }

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

  if (timings.next_rx_point != TimePoint{}) {
    planned_send_at_ = timings.next_rx_point;
  }

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
  rx_window_held_ = false;
  CloseLocalRx(local_rx_);
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

          Duration min_rtt{};
          Duration p99_rtt{};
          if (!response_stats.empty()) {
            min_rtt = response_stats.min();
            p99_rtt = response_stats.template percentile<99>();
          }

          if (required_rx_until_.has_value() &&
              *required_rx_until_ <= Now() && !local_rx_.open) {
            required_rx_until_.reset();
          }

          auto const actual_send_at = Now();
          EarlyRxWindowInput window_in{};
          window_in.has_planned_send = planned_send_at_.has_value();
          if (planned_send_at_.has_value()) {
            window_in.planned_send_at = *planned_send_at_;
          }
          window_in.actual_send_at = actual_send_at;
          window_in.base_rx_window = timing_conf_.rx_window;
          window_in.has_required_rx_until = required_rx_until_.has_value();
          if (required_rx_until_.has_value()) {
            window_in.required_rx_until = *required_rx_until_;
          }
          auto const window_out = ComputeEarlyRxWindow(window_in);

          PingAttempt attempt{};
          attempt.server_id = cloud_sc_->server_id();
          attempt.planned_send_at = planned_send_at_;
          attempt.actual_send_at = actual_send_at;
          attempt.early_by = window_out.early_by;
          attempt.base_rx_window = timing_conf_.rx_window;
          attempt.effective_wire_rx_window = window_out.effective_wire_rx_window;
          attempt.required_rx_until_before = required_rx_until_;
          attempt.required_rx_until = window_out.required_rx_until;
          attempt.min_rtt = min_rtt;
          attempt.p99_rtt = p99_rtt;
          attempt.ping_guard = guard;
          attempt.channel_generation = ++send_generation_;

          if (timing_conf_.interval > Duration{}) {
            next_ping_time_ = actual_send_at + timing_conf_.interval - guard;
          } else {
            next_ping_time_ = TimePoint::max();
          }
          attempt.next_planned_send = next_ping_time_;
          required_rx_until_ = window_out.required_rx_until;
          in_flight_ = attempt;
          EmitTrace(PingTraceKind::kPrepared);

          ping_.emplace(ae_context_, *cloud_sc_, timing_conf_.interval,
                        window_out.effective_wire_rx_window,
                        c->ResponseTimeout());

          ping_blocker_ = policy_->AcquireSuspendBlock();
          ping_->result_event().Subscribe(
              [this](Ping::PingResult const& res) noexcept {
                OnPingResult(res);
                ping_blocker_.Reset();
              });

          ping_->Start(actual_send_at);
          OpenRxWindow();
          EmitTrace(PingTraceKind::kSent);
          if (timing_conf_.interval > Duration{}) {
            policy_->ReportNextServiceTime(priority_, next_ping_time_);
          }
          AE_TELED_DEBUG(
              "Next ping time for priority {} at {} after {} (guard {}) "
              "early_by {} wire_rx {}",
              priority_, next_ping_time_, timing_conf_.interval, guard,
              window_out.early_by, window_out.effective_wire_rx_window);

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
            planned_send_at_ = next_ping_time_;
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

  auto const wire = in_flight_.has_value() ? in_flight_->effective_wire_rx_window
                                           : timing_conf_.rx_window;

  std::visit(
      [this, c, wire](auto const& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Ok<Duration>>) {
          c->channel_statistics().AddResponseTime(value.value);
          ScheduleRxWindowClose(ComputeRxWindowCloseTime(Now(), wire));
          EmitTrace(PingTraceKind::kResult, 0);
        } else if constexpr (std::is_same_v<T, Ping::LateDuration>) {
          AE_TELED_DEBUG("Got late ping duration");
          c->channel_statistics().AddResponseTime(value.duration);
          ScheduleRxWindowClose(ComputeRxWindowCloseTime(Now(), wire));
          EmitTrace(PingTraceKind::kResult, 4);
        } else if constexpr (std::is_same_v<T, Error<int>>) {
          AE_TELED_ERROR("Ping error!");
          if (value.error == 1) {
            MaybeCloseAfterWriteFailure();
            EmitTrace(PingTraceKind::kResult, 1);
          } else {
            ScheduleRxWindowClose(ComputeRxWindowCloseTime(Now(), wire));
            EmitTrace(PingTraceKind::kResult, value.error);
          }
          ScheduleRestream();
        } else {
          AE_TELED_ERROR("Ping error!");
          ScheduleRxWindowClose(ComputeRxWindowCloseTime(Now(), wire));
          EmitTrace(PingTraceKind::kResult, 3);
          ScheduleRestream();
        }
      },
      res);
}

void PingCloudServers::ServerPing::OpenRxWindow() {
  if (!rx_window_held_) {
    rx_window_blocker_ = policy_->AcquireSuspendBlock();
    rx_window_held_ = true;
  }
}

void PingCloudServers::ServerPing::ScheduleRxWindowClose(TimePoint close_time) {
  OpenRxWindow();
  if (!ExtendLocalRxUntil(local_rx_, close_time)) {
    return;
  }
  auto const gen = local_rx_.generation;
  rx_window_sub_ = ae_context_.scheduler().DelayedTask(
      [this, gen]() {
        if (!ShouldApplyCloseTimer(local_rx_, gen, Now())) {
          return;
        }
        CloseRxWindowNow();
        EmitTrace(PingTraceKind::kRxClosed);
      },
      close_time);
  EmitTrace(PingTraceKind::kRxCloseScheduled);
}

void PingCloudServers::ServerPing::CloseRxWindowNow() {
  rx_window_sub_.Reset();
  rx_window_blocker_.Reset();
  rx_window_held_ = false;
  CloseLocalRx(local_rx_);
}

void PingCloudServers::ServerPing::MaybeCloseAfterWriteFailure() {
  if (in_flight_.has_value()) {
    in_flight_->write_failed = true;
    required_rx_until_ = in_flight_->required_rx_until_before;
  }
  auto const now = Now();
  if (ShouldCloseLocalRxAfterWriteFailure(
          local_rx_, required_rx_until_.has_value(),
          required_rx_until_.value_or(TimePoint{}), now)) {
    CloseRxWindowNow();
  }
}

void PingCloudServers::ServerPing::EmitTrace(PingTraceKind kind,
                                             int result_type) const {
  if (g_ping_trace_hook == nullptr || !in_flight_.has_value()) {
    return;
  }
  auto const& a = *in_flight_;
  PingTraceEvent event{};
  event.kind = kind;
  event.server_id = a.server_id;
  event.planned_send_at = a.planned_send_at.value_or(a.actual_send_at);
  event.actual_send_at = a.actual_send_at;
  event.early_by = a.early_by;
  event.base_rx_window = a.base_rx_window;
  event.effective_wire_rx_window = a.effective_wire_rx_window;
  event.required_rx_until = a.required_rx_until;
  event.next_planned_send = a.next_planned_send;
  event.min_rtt = a.min_rtt;
  event.p99_rtt = a.p99_rtt;
  event.ping_guard = a.ping_guard;
  event.channel_generation = a.channel_generation;
  event.result_type = result_type;
  event.event_time = Now();
  g_ping_trace_hook(event);
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
