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
#include <limits>
#include <type_traits>
#include <variant>

#if AE_ENABLE_PING

#  include "aether/channels/channel.h"
#  include "aether/executors/executors.h"
#  if AE_ENABLE_PING_TEST_FAULTS
#    include "aether/ae_actions/ping_test_faults.h"
#  endif

#  include "aether/cloud_connections/cloud_connections_tele.h"

namespace ae {
namespace {
PingTraceHook g_ping_trace_hook{nullptr};
}  // namespace

void SetPingTraceHook(PingTraceHook hook) noexcept { g_ping_trace_hook = hook; }

PingCloudServers::ServerPing::ServerPing(AeContext const& ae_context,
                                         PingCloudServers& owner,
                                         ClientConnectivityPolicy& policy,
                                         CloudServerConnection& cloud_sc,
                                         std::size_t priority)
    : ae_context_{ae_context},
      owner_{&owner},
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

  auto& st = Cycle();
  if (st.required_rx_until != TimePoint{}) {
    required_rx_until_ = st.required_rx_until;
  }
  if (st.active && !st.confirmed) {
    if (st.first_attempt_at != TimePoint{}) {
      planned_send_at_ = st.first_attempt_at;
    }
    ping_blocker_ = policy_->AcquireSuspendBlock();
    start_sub_ = ae_context_.scheduler().Task([&]() { Start(); });
    return;
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

          auto const& response_stats =
              c->channel_statistics().response_time_statistics();
          Duration min_rtt = kPingRttEstimate;
          Duration p99_rtt = kPingRttEstimate;
          if (!response_stats.empty()) {
            min_rtt = response_stats.min();
            p99_rtt = response_stats.template percentile<99>();
          }
          auto const guard = ResolvePingSendGuard(min_rtt, p99_rtt,
                                                  timing_conf_.interval);
          auto const budget = ComputePingRetryBudget(PingRetryBudgetInput{
              timing_conf_.interval,
              guard,
              c->ResponseTimeout(),
              p99_rtt,
              policy_->ping_retry_count()});

          if (required_rx_until_.has_value() &&
              *required_rx_until_ <= Now() && !local_rx_.open) {
            required_rx_until_.reset();
          }

          auto& st = Cycle();
#if AE_ENABLE_PING_TEST_FAULTS
          // Test-only: hold a same-cycle retry until a coordinator-chosen
          // send time relative to the original nominal Tn. Does not run when
          // no hold plan is armed.
          if (st.active && !st.confirmed && !announce_unknown_) {
            auto const next_attempt = st.attempt_index + 1;
            auto const hold_us = PingTestFaults::Instance().RetryHoldOffsetUs(
                cloud_sc_->server_id(), st.cycle_id, next_attempt);
            if (hold_us.has_value() && st.nominal_ping_at != TimePoint{}) {
              TimePoint target = st.nominal_ping_at;
              if (*hold_us >= 0) {
                target = SaturatingAddTime(
                    st.nominal_ping_at,
                    Duration{static_cast<Duration::rep>(*hold_us)});
              } else {
                auto mag = static_cast<std::uint64_t>(-*hold_us);
                auto const max_rep = static_cast<std::uint64_t>(
                    std::numeric_limits<Duration::rep>::max());
                if (mag > max_rep) {
                  mag = max_rep;
                }
                target = SaturatingSubDuration(
                    st.nominal_ping_at,
                    Duration{static_cast<Duration::rep>(mag)});
              }
              if (Now() < target) {
                start_sub_ = ae_context_.scheduler().DelayedTask(
                    [this]() { Start(); }, target);
                return ex::set_value(std::move(ctx.receiver));
              }
            }
          }
#endif
          auto const actual_send_at = Now();
          if (!announce_unknown_ && owner_->auto_ping_enabled_ &&
              st.confirmed && actual_send_at < st.next_local_send_at) {
            return ex::set_value(std::move(ctx.receiver));
          }
          if (required_rx_until_.has_value()) {
            st.required_rx_until = *required_rx_until_;
          }
          LogicalPingAttemptRequest attempt_req{};
          attempt_req.actual_send_at = actual_send_at;
          attempt_req.interval = timing_conf_.interval;
          attempt_req.guard = guard;
          attempt_req.attempt_lead = budget.attempt_lead;
          attempt_req.retry_reserve = budget.retry_reserve;
          attempt_req.loss_timeout = budget.loss_timeout;
          attempt_req.base_rx_window = timing_conf_.rx_window;
          attempt_req.predeadline_retry_guaranteed =
              budget.predeadline_retry_guaranteed;
          attempt_req.announce_unknown = announce_unknown_;
          auto const view = ApplyLogicalPingAttempt(st, attempt_req);
          if (view.started_new_cycle) {
            // Freeze the R99 used for this logical cycle's expected response
            // deadline so later RTT samples do not slide it.
            st.frozen_p99_rtt = p99_rtt;
            st.has_frozen_p99_rtt = true;
#if AE_ENABLE_PING_TEST_FAULTS
            PingTestFaults::Instance().BindNextCycle(cloud_sc_->server_id(),
                                                     view.cycle_id);
#endif
            EmitTrace(PingTraceKind::kCycleStarted);
          }

          Duration timeout = budget.loss_timeout;
          bool request_sent = true;
          bool response_ignored = false;
          std::int32_t fault_mode = 0;
#if AE_ENABLE_PING_TEST_FAULTS
          auto const fault = PingTestFaults::Instance().Consume(PingFaultContext{
              cloud_sc_->server_id(), view.cycle_id, view.attempt_index,
              view.first_attempt_at, actual_send_at});
          if (fault.timeout_override.count() > 0) {
            timeout = fault.timeout_override;
          }
          request_sent = fault.mode != PingFaultMode::kDropRequest;
          response_ignored = fault.mode == PingFaultMode::kIgnoreResponse;
          fault_mode = static_cast<std::int32_t>(fault.mode);
#endif

          PingAttempt attempt{};
          attempt.server_id = cloud_sc_->server_id();
          attempt.planned_send_at = planned_send_at_;
          attempt.actual_send_at = actual_send_at;
          attempt.early_by = view.rx.early_by;
          attempt.base_rx_window = timing_conf_.rx_window;
          attempt.effective_wire_rx_window = view.rx.effective_wire_rx_window;
          attempt.required_rx_until_before = required_rx_until_;
          attempt.required_rx_until = view.rx.required_rx_until;
          attempt.min_rtt = min_rtt;
          attempt.p99_rtt = p99_rtt;
          attempt.ping_guard = guard;
          attempt.channel_generation = ++send_generation_;
          attempt.logical_cycle_id = view.cycle_id;
          attempt.physical_attempt_index = view.attempt_index;
          attempt.fault_mode = fault_mode;
          attempt.request_was_sent = request_sent;
          attempt.response_was_ignored = response_ignored;
          attempt.cycle_anchor = view.cycle_anchor;
          attempt.contract_deadline = view.contract_deadline;
          attempt.wire_next_connect_ms = view.wire_next_connect_ms;
          attempt.retry_delay =
              view.is_retry
                  ? SaturatingSubTime(actual_send_at, st.first_attempt_at)
                  : Duration{};
          attempt.attempt_lead = view.attempt_lead;
          attempt.retry_reserve = view.retry_reserve;
          attempt.loss_timeout = view.loss_timeout;
          attempt.predeadline_retry_guaranteed =
              view.predeadline_retry_guaranteed;
          attempt.next_local_send_at = view.next_local_send;

          next_ping_time_ = view.next_local_send;
          attempt.next_planned_send = next_ping_time_;
          required_rx_until_ = view.rx.required_rx_until;
          in_flight_ = attempt;
          EmitTrace(PingTraceKind::kAttemptPrepared);
          EmitTrace(PingTraceKind::kPrepared);

          ping_.emplace(ae_context_, *cloud_sc_, view.wire_next_connect,
                        view.rx.effective_wire_rx_window, timeout);
#if AE_ENABLE_PING_TEST_FAULTS
          ping_->ApplyTestFault(static_cast<std::uint8_t>(fault_mode));
#endif

          if (request_sent) {
            ping_blocker_ = policy_->AcquireSuspendBlock();
          }
          ping_->result_event().Subscribe(
              [this](Ping::PingResult const& res) noexcept {
                OnPingResult(res);
                ping_blocker_.Reset();
              });

          ping_->Start(actual_send_at);
          if (request_sent) {
            OpenRxWindow();
            EmitTrace(PingTraceKind::kSent);
            EmitTrace(PingTraceKind::kRequestSent);
            if (response_ignored) {
              EmitTrace(PingTraceKind::kResponseIgnored);
            }
          } else {
            EmitTrace(PingTraceKind::kRequestDropped);
          }
          if (timing_conf_.interval > Duration{}) {
            policy_->ReportNextServiceTime(priority_, next_ping_time_);
          }
          AE_TELED_DEBUG(
              "Next ping time for priority {} at {} after {} (guard {}) "
              "early_by {} wire_rx {} cycle {} attempt {}",
              priority_, next_ping_time_, timing_conf_.interval, guard,
              view.rx.early_by, view.rx.effective_wire_rx_window, view.cycle_id,
              view.attempt_index);

          return ex::set_value(std::move(ctx.receiver));
        });
  });
}

void PingCloudServers::ServerPing::Start() {
  if (stop_) {
    return;
  }
  auto const& st = Cycle();
  if (!announce_unknown_ && owner_->auto_ping_enabled_ && st.confirmed &&
      Now() < st.next_local_send_at) {
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
          // Next logical cycle is scheduled only after confirmation.
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

  auto& st = Cycle();
  auto const attempt =
      in_flight_.has_value() ? in_flight_->physical_attempt_index : 0;
  auto const cycle_id =
      in_flight_.has_value() ? in_flight_->logical_cycle_id : 0;
  auto const wire = in_flight_.has_value() ? in_flight_->effective_wire_rx_window
                                           : timing_conf_.rx_window;
  bool const request_sent =
      !in_flight_.has_value() || in_flight_->request_was_sent;

  std::visit(
      [this, c, wire, attempt, cycle_id, request_sent, &st](auto const& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Ok<Duration>> ||
                      std::is_same_v<T, Ping::LateDuration>) {
          if (!ShouldAcceptCycleResult(st.active, st.confirmed, st.cycle_id,
                                       cycle_id, st.attempt_index, attempt,
                                       st.current_attempt_timed_out, true)) {
            return;
          }
          if constexpr (std::is_same_v<T, Ok<Duration>>) {
            c->channel_statistics().AddResponseTime(value.value);
            EmitTrace(PingTraceKind::kResult, 0);
          } else {
            AE_TELED_DEBUG("Got late ping duration");
            c->channel_statistics().AddResponseTime(value.duration);
            EmitTrace(PingTraceKind::kResult, 4);
          }
          if (request_sent) {
            ScheduleRxWindowClose(ComputeRxWindowCloseTime(Now(), wire));
          }
          ConfirmCycleAndScheduleNext();
        } else if constexpr (std::is_same_v<T, Error<int>>) {
          if (!ShouldAcceptCycleResult(st.active, st.confirmed, st.cycle_id,
                                       cycle_id, st.attempt_index, attempt,
                                       st.current_attempt_timed_out, false)) {
            return;
          }
          AE_TELED_ERROR("Ping error!");
          if (value.error == 2) {
            MarkLogicalPingAttemptTimedOut(st);
            EmitTrace(PingTraceKind::kAttemptTimeout, 2);
            EmitTrace(PingTraceKind::kResult, 2);
            // Keep the RX window open: a late pong or same-cycle retry still
            // needs it. Closing here drops the next attempt's response.
            ScheduleSameCycleRetryWithPreDeadlinePolicy(false);
            return;
          }
          if (value.error == 1) {
            MaybeCloseAfterWriteFailure();
            st.required_rx_until = required_rx_until_.value_or(TimePoint{});
            EmitTrace(PingTraceKind::kResult, 1);
            ScheduleSameCycleRetryWithPreDeadlinePolicy(true);
            return;
          }
          if (announce_unknown_) {
            owner_->OnAnnounceServerDone(false);
            return;
          }
          if (request_sent) {
            ScheduleRxWindowClose(ComputeRxWindowCloseTime(Now(), wire));
          }
          EmitTrace(PingTraceKind::kResult, value.error);
          st.active = false;
          st.confirmed = false;
          ScheduleRestream();
          if (!owner_->auto_ping_enabled_) {
            return;
          }
          planned_send_at_ = st.next_local_send_at;
          next_ping_time_ = st.next_local_send_at;
          if (timing_conf_.interval > Duration{}) {
            policy_->ReportNextServiceTime(priority_, next_ping_time_);
            start_sub_ = ae_context_.scheduler().DelayedTask(
                [this]() { Start(); }, next_ping_time_);
          }
        } else {
          AE_TELED_ERROR("Ping error!");
          if (request_sent) {
            ScheduleRxWindowClose(ComputeRxWindowCloseTime(Now(), wire));
          }
          EmitTrace(PingTraceKind::kResult, 3);
          st.active = false;
          st.confirmed = false;
          if (announce_unknown_) {
            owner_->OnAnnounceServerDone(false);
            return;
          }
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
  event.logical_cycle_id = a.logical_cycle_id;
  event.physical_attempt_index = a.physical_attempt_index;
  event.fault_mode = a.fault_mode;
  event.request_was_sent = a.request_was_sent;
  event.response_was_ignored = a.response_was_ignored;
  event.cycle_anchor = a.cycle_anchor;
  event.contract_deadline = a.contract_deadline;
  event.wire_next_connect_ms = a.wire_next_connect_ms;
  event.retry_delay = a.retry_delay;
  event.next_local_send_at = a.next_local_send_at;
  event.attempt_lead = a.attempt_lead;
  event.retry_reserve = a.retry_reserve;
  event.loss_timeout = a.loss_timeout;
  event.predeadline_retry_guaranteed = a.predeadline_retry_guaranteed;
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

LogicalPingCycleState& PingCloudServers::ServerPing::Cycle() {
  return owner_->cycle_states_[cloud_sc_->server_id()];
}

void PingCloudServers::ServerPing::ConfirmCycleAndScheduleNext() {
  auto& st = Cycle();
  ConfirmLogicalPingCycle(st);
  EmitTrace(PingTraceKind::kCycleConfirmed);
  if (announce_unknown_) {
    announce_unknown_ = false;
    owner_->OnAnnounceServerDone(true);
    return;
  }
  if (!owner_->auto_ping_enabled_ || timing_conf_.interval == Duration{}) {
    return;
  }
  planned_send_at_ = st.next_local_send_at;
  next_ping_time_ = st.next_local_send_at;
  policy_->ReportNextServiceTime(priority_, next_ping_time_);
  EmitTrace(PingTraceKind::kNextCycleScheduled);
  if (Now() >= next_ping_time_) {
    start_sub_ = ae_context_.scheduler().Task([this]() { Start(); });
  } else {
    start_sub_ = ae_context_.scheduler().DelayedTask([this]() { Start(); },
                                                     next_ping_time_);
  }
}

bool PingCloudServers::ServerPing::ChannelLinkedAndWritable() const {
  auto* cc = cloud_sc_->client_connection();
  if (cc == nullptr) {
    return false;
  }
  auto const info = cc->stream_info();
  return info.link_state == LinkState::kLinked && info.is_writable;
}

void PingCloudServers::ServerPing::ScheduleSameCycleRetryWithPreDeadlinePolicy(
    bool restream_first) {
  if (stop_) {
    return;
  }
  if (announce_unknown_) {
    ScheduleSameCycleRetry(restream_first);
    return;
  }
  auto& st = Cycle();
  auto const now = Now();
  auto const deadline = st.next_nominal_ping_at;
  if (CanSchedulePreDeadlineSameCycleRetry(now, deadline, st.attempt_index,
                                           policy_->ping_retry_count())) {
    ScheduleSameCycleRetry(restream_first);
    return;
  }
  if (deadline != TimePoint{} && now < deadline) {
    EmitTrace(PingTraceKind::kRetryScheduled);
    start_sub_ = ae_context_.scheduler().DelayedTask(
        [this, restream_first]() {
          ScheduleSameCycleRetry(restream_first);
        },
        deadline);
    return;
  }
  ScheduleSameCycleRetry(restream_first);
}

void PingCloudServers::ServerPing::ScheduleSameCycleRetry(bool restream_first) {
  if (stop_) {
    return;
  }
  auto& st = Cycle();
  st.awaiting_relink_retry = restream_first || !ChannelLinkedAndWritable();
  EmitTrace(PingTraceKind::kRetryScheduled);
  auto kick_start = [this]() {
    start_sub_ = ae_context_.scheduler().Task([this]() { Start(); });
  };
  if (restream_first) {
    restream_blocker_ = policy_->AcquireSuspendBlock();
    restream_sub_ = ae_context_.scheduler().Task([this, kick_start]() {
      auto* cc = cloud_sc_->client_connection();
      if (cc != nullptr) {
        cc->Restream();
      }
      restream_blocker_.Reset();
      cc = cloud_sc_->client_connection();
      if (cc != nullptr &&
          cc->stream_info().link_state == LinkState::kLinked) {
        kick_start();
      } else if (cc != nullptr) {
        WaitForLink(*cc, kick_start);
      } else {
        kick_start();
      }
    });
    return;
  }
  if (ChannelLinkedAndWritable()) {
    kick_start();
    return;
  }
  auto* cc = cloud_sc_->client_connection();
  if (cc != nullptr) {
    WaitForLink(*cc, kick_start);
  } else {
    kick_start();
  }
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
        server_id,
        std::make_unique<ServerPing>(ae_context_, *this, *policy_, cloud_sc,
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

void PingCloudServers::ServerPing::AnnounceUnknown() {
  announce_unknown_ = true;
  start_sub_.Reset();
  ping_blocker_ = policy_->AcquireSuspendBlock();
  start_sub_ = ae_context_.scheduler().Task([this]() { Start(); });
}

bool PingCloudServers::ServerPing::quarantined() const noexcept {
  return cloud_sc_ != nullptr && cloud_sc_->quarantine();
}

void PingCloudServers::StopAutomaticPing() noexcept {
  auto_ping_enabled_ = false;
}

std::optional<TimePoint> PingCloudServers::expected_ping_response_time()
    const noexcept {
  if (!auto_ping_enabled_) {
    return std::nullopt;
  }
  std::optional<TimePoint> best;
  for (auto const& [server_id, st] : cycle_states_) {
    (void)server_id;
    auto const expected = ExpectedPingResponseTimeForCycle(st);
    if (!expected.has_value()) {
      continue;
    }
    if (!best.has_value() || *expected < *best) {
      best = expected;
    }
  }
  return best;
}

PingCloudServers::AnnounceEvent::Subscriber
PingCloudServers::announce_event() {
  return EventSubscriber{announce_event_};
}

void PingCloudServers::BeginAnnounceUnknown() {
  StopAutomaticPing();
  if (announce_in_progress_) {
    return;
  }
  announce_in_progress_ = true;
  announce_any_ok_ = false;
  announce_pending_ = 0;
  for (auto& [id, sp] : server_pings_) {
    if (sp == nullptr || sp->stopped() || sp->quarantined()) {
      continue;
    }
    ++announce_pending_;
    sp->AnnounceUnknown();
  }
  if (announce_pending_ == 0) {
    announce_in_progress_ = false;
    announce_event_.Emit(Ok{std::monostate{}});
  }
}

void PingCloudServers::OnAnnounceServerDone(bool ok) {
  if (!announce_in_progress_) {
    return;
  }
  if (ok) {
    announce_any_ok_ = true;
  }
  if (announce_pending_ > 0) {
    --announce_pending_;
  }
  if (announce_pending_ == 0) {
    announce_in_progress_ = false;
    if (announce_any_ok_) {
      announce_event_.Emit(Ok{std::monostate{}});
    } else {
      announce_event_.Emit(Error{1});
    }
  }
}
}  // namespace ae

#endif
