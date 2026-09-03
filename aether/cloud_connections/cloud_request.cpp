/*
 * Copyright 2025 Aethernet Inc.
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

#include "aether/cloud_connections/cloud_request.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "aether-miscpp/misc/override.h"
#include "aether/aether.h"
#include "aether/channels/channel.h"
#include "aether/server.h"
#include "aether/write_action/write_action.h"

#include "aether/cloud_connections/cloud_connections_tele.h"

namespace ae {
namespace {

#if defined(AE_TELE_ENABLED) && AE_TELE_ENABLED
#  define AE_CLOUD_REQ_DEBUG(...) AE_TELED_DEBUG(__VA_ARGS__)
#  define AE_CLOUD_REQ_WARNING(...) AE_TELED_WARNING(__VA_ARGS__)
#  define AE_CLOUD_REQ_ERROR(...) AE_TELED_ERROR(__VA_ARGS__)
#else
#  define AE_CLOUD_REQ_DEBUG(...)
#  define AE_CLOUD_REQ_WARNING(...)
#  define AE_CLOUD_REQ_ERROR(...)
#endif

}  // namespace

CloudRequest::CloudRequest(AeContext const& ae_context,
                           ApiCallWithListener&& api_call,
                           CloudServerConnections& cloud_server_connections,
                           RequestPolicy::Variant policy,
                           CloudRequestExecutionPolicy exec_policy)
    : ae_context_{ae_context},
      request_{std::move(api_call)},
      cloud_scs_{&cloud_server_connections},
      policy_{policy},
      exec_policy_{exec_policy},
      server_changed_sub_{cloud_scs_->servers_update_event().Subscribe(
          MethodPtr<&CloudRequest::ServersUpdated>{this})} {
  NormalizeCloudRequestExecutionPolicy(exec_policy_);
  AE_CLOUD_REQ_DEBUG(
      "CLOUD_REQUEST_START percentile_code={} factor_raw={} retry_count={} "
      "hedge_next_servers={}",
      exec_policy_.response_percentile.Code(),
      exec_policy_.timeout_factor.RawValue(), exec_policy_.retry_count,
      exec_policy_.hedge_next_servers);
  RebuildCandidates();
  ActivateInitial();
  EnqueuePump();
}

CloudRequest::CloudRequest(AeContext const& ae_context,
                           ApiRequestHandler&& api_request,
                           CloudServerConnections& cloud_server_connections,
                           RequestPolicy::Variant policy,
                           CloudRequestExecutionPolicy exec_policy)
    : ae_context_{ae_context},
      request_{std::move(api_request)},
      cloud_scs_{&cloud_server_connections},
      policy_{policy},
      exec_policy_{exec_policy},
      server_changed_sub_{cloud_scs_->servers_update_event().Subscribe(
          MethodPtr<&CloudRequest::ServersUpdated>{this})} {
  NormalizeCloudRequestExecutionPolicy(exec_policy_);
  AE_CLOUD_REQ_DEBUG(
      "CLOUD_REQUEST_START percentile_code={} factor_raw={} retry_count={} "
      "hedge_next_servers={}",
      exec_policy_.response_percentile.Code(),
      exec_policy_.timeout_factor.RawValue(), exec_policy_.retry_count,
      exec_policy_.hedge_next_servers);
  RebuildCandidates();
  ActivateInitial();
  EnqueuePump();
}

void CloudRequest::Succeeded() {
  Finish();
  result_event_.Emit(true);
}

void CloudRequest::Failed() {
  Finish();
  result_event_.Emit(false);
}

void CloudRequest::StopServerTimers(ServerRequest& sr) {
  for (auto& attempt : sr.attempts) {
    attempt.timeout_sub.Reset();
  }
  sr.channel_changed_sub.Reset();
}

void CloudRequest::SucceedAttempt(CloudServerConnection* sc) {
  auto it = server_requests_.find(sc);
  if (it == server_requests_.end()) {
    return;
  }
  auto& sr = it->second;
  if (sr.exec.IsTerminal()) {
    return;
  }
  AE_CLOUD_REQ_DEBUG("SERVER_ATTEMPT_SUCCESS server_id={}", sc->server_id());
  StopServerTimers(sr);
  sr.exec.MarkSucceeded();
  ActivateFollowing(1);
  EnqueuePump();
}

void CloudRequest::CompleteAttemptWithRemoteError(CloudServerConnection* sc) {
  auto it = server_requests_.find(sc);
  if (it == server_requests_.end()) {
    return;
  }
  auto& sr = it->second;
  if (sr.exec.IsTerminal()) {
    return;
  }
  // Authenticated API error: server proved it is alive. Do not treat as
  // no-response, do not soft-retry, do not quarantine for timeout policy.
  AE_CLOUD_REQ_DEBUG(
      "SERVER_REMOTE_API_ERROR server_id={} (no no-response quarantine)",
      sc->server_id());
  StopServerTimers(sr);
  sr.exec.MarkRemoteErrorCompleted();
  ActivateFollowing(1);
  EnqueuePump();
}

CloudRequest::ResultEvent::Subscriber CloudRequest::result_event() {
  return EventSubscriber{result_event_};
}

CloudRequest::AttemptExhaustedEvent::Subscriber
CloudRequest::attempt_exhausted_event() {
  return EventSubscriber{attempt_exhausted_event_};
}

void CloudRequest::EmitAttemptExhausted(CloudServerConnection* sc) {
  attempt_exhausted_event_.Emit(sc);
}

void CloudRequest::RebuildCandidates() {
  std::vector<CloudServerConnection*> next;
  cloud_scs_->ForServers([&](CloudServerConnection* sc) { next.push_back(sc); },
                         policy_);
  for (auto* sc : next) {
    auto const known =
        std::find(candidates_.begin(), candidates_.end(), sc) !=
        candidates_.end();
    if (!known) {
      candidates_.push_back(sc);
      server_requests_.emplace(sc, ServerRequest{});
    }
  }
}

void CloudRequest::ActivateInitial() {
  if (candidates_.empty()) {
    return;
  }
  ActivateFollowing(1);
}

void CloudRequest::ActivateFollowing(std::uint8_t count, bool as_hedge,
                                     CloudServerConnection* source) {
  while (count > 0 && activate_cursor_ < candidates_.size()) {
    auto* sc = candidates_[activate_cursor_++];
    auto& sr = server_requests_[sc];
    if (sr.exec.activated || sr.exec.IsTerminal()) {
      continue;
    }
    if (as_hedge) {
      AE_CLOUD_REQ_DEBUG(
          "SERVER_HEDGE_ACTIVATED source_server={} new_server={}",
          source != nullptr ? source->server_id() : ServerId{}, sc->server_id());
    }
    ActivateServer(sc);
    --count;
  }
}

void CloudRequest::ActivateServer(CloudServerConnection* sc) {
  auto& sr = server_requests_[sc];
  if (sr.exec.activated) {
    return;
  }
  sr.exec.activated = true;
  EnsureChannelChangedSubscription(sc, sr);
  LaunchAttempt(sc, sr);
}

void CloudRequest::EnsureChannelChangedSubscription(CloudServerConnection* sc,
                                                    ServerRequest& sr) {
  if (sr.channel_changed_sub) {
    return;
  }
  auto* conn = sc->client_connection();
  if (conn == nullptr) {
    return;
  }
  sr.channel_changed_sub =
      conn->server_connection().channel_changed_event().Subscribe([this, sc]() {
        AE_CLOUD_REQ_WARNING("Request server channel changed {}",
                             sc->server_id());
        OnChannelChanged(sc);
      });
}

Duration CloudRequest::SoftTimeoutFor(CloudServerConnection* sc) const {
  auto* conn = sc->client_connection();
  if (conn == nullptr) {
    return ComputeCloudRequestSoftTimeout(FallbackCloudRequestRtt(),
                                          exec_policy_);
  }
  auto channel = conn->server_connection().current_channel();
  if (!channel) {
    return ComputeCloudRequestSoftTimeout(FallbackCloudRequestRtt(),
                                          exec_policy_);
  }
  auto const& stats =
      channel->channel_statistics().response_time_statistics();
  if (stats.empty()) {
    return ComputeCloudRequestSoftTimeout(FallbackCloudRequestRtt(),
                                          exec_policy_);
  }
  auto const rtt =
      stats.PercentileValue(exec_policy_.response_percentile);
  return ComputeCloudRequestSoftTimeout(rtt, exec_policy_);
}

void CloudRequest::LaunchAttempt(CloudServerConnection* sc,
                                 ServerRequest& sr) {
  auto const attempt_index = sr.exec.StartAttempt(exec_policy_);
  if (attempt_index == 0) {
    return;
  }

  auto* conn = sc->client_connection();
  if (conn == nullptr) {
    AE_CLOUD_REQ_WARNING("SERVER_ATTEMPT skipped disconnected server {}",
                         sc->server_id());
    auto const action = sr.exec.OnSoftTimeout(exec_policy_);
    if (action == CloudRequestServerExecState::SoftTimeoutAction::kExhaust) {
      ExhaustServerNoResponse(sc, sr);
    } else if (action ==
               CloudRequestServerExecState::SoftTimeoutAction::kRetry) {
      sr.exec.attempts_started =
          static_cast<std::uint8_t>(sr.exec.attempts_started - 1);
    }
    return;
  }

  EnsureChannelChangedSubscription(sc, sr);

  auto const timeout = SoftTimeoutFor(sc);
  AE_CLOUD_REQ_DEBUG(
      "SERVER_ATTEMPT server_id={} attempt_index={} timeout_ms={}",
      sc->server_id(), attempt_index,
      std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());

  AttemptState attempt{};
  attempt.attempt_index = attempt_index;

  auto& swa =
      std::visit(Override{
                     [&](ApiCallWithListener& api_call) -> decltype(auto) {
                       return conn->AuthorizedApiCall(
                           SubApi{[&](ApiContext<AuthorizedApi>& api) {
                             api_call.call(api, sc);
                           }});
                     },
                     [&](ApiRequestHandler& api_request) -> decltype(auto) {
                       return conn->AuthorizedApiCall(
                           SubApi{[&](ApiContext<AuthorizedApi>& api) {
                             api_request(api, sc, this);
                           }});
                     },
                 },
                 request_);

  // Write failure: send may not have reached the server. Reuse soft retry
  // budget (no Restream). LinkError quarantine remains on the connection
  // health path and is not duplicated here beyond ExhaustServerNoResponse.
  attempt.write_subs += swa.status_event().Subscribe([this, sc](auto status) {
    if (status == WriteAction::Status::kFail) {
      AE_CLOUD_REQ_WARNING("Request write error server {}", sc->server_id());
      OnWriteFailed(sc);
    }
  });

  if (std::holds_alternative<ApiCallWithListener>(request_)) {
    auto& listener = std::get<ApiCallWithListener>(request_).listener;
    if (listener) {
      sr.response_subs += listener(conn->client_safe_api(), sc, this);
    }
  }

  attempt.timeout_sub = ae_context_.scheduler().DelayedTask(
      [this, sc, attempt_index]() { OnSoftTimeout(sc, attempt_index); },
      timeout);

  sr.attempts.push_back(std::move(attempt));
}

void CloudRequest::OnSoftTimeout(CloudServerConnection* sc,
                                 std::uint8_t attempt_index) {
  auto it = server_requests_.find(sc);
  if (it == server_requests_.end()) {
    return;
  }
  auto& sr = it->second;
  if (sr.exec.IsTerminal()) {
    return;
  }

  for (auto& attempt : sr.attempts) {
    if (attempt.attempt_index == attempt_index) {
      attempt.timed_out = true;
      attempt.timeout_sub.Reset();
      break;
    }
  }

  AE_CLOUD_REQ_DEBUG(
      "SERVER_SOFT_TIMEOUT server_id={} attempt_index={} (no Restream)",
      sc->server_id(), attempt_index);

  bool const first_soft_miss = !sr.exec.first_soft_miss_seen;
  auto const action = sr.exec.OnSoftTimeout(exec_policy_);
  if (first_soft_miss && exec_policy_.hedge_next_servers > 0) {
    ActivateFollowing(exec_policy_.hedge_next_servers, /*as_hedge=*/true, sc);
  }

  if (action == CloudRequestServerExecState::SoftTimeoutAction::kRetry) {
    LaunchAttempt(sc, sr);
    EnqueuePump();
    return;
  }
  if (action == CloudRequestServerExecState::SoftTimeoutAction::kExhaust) {
    ExhaustServerNoResponse(sc, sr);
    EnqueuePump();
    return;
  }
  EnqueuePump();
}

void CloudRequest::ExhaustServerNoResponse(CloudServerConnection* sc,
                                           ServerRequest& sr) {
  if (sr.exec.succeeded || sr.exec.remote_error_completed) {
    return;
  }
  sr.exec.MarkExhausted();
  StopServerTimers(sr);
  AE_CLOUD_REQ_WARNING(
      "SERVER_RETRY_EXHAUSTED server_id={} attempts={} soft_timeouts={} -> "
      "SERVER_QUARANTINE_NO_RESPONSE",
      sc->server_id(), sr.exec.attempts_started, sr.exec.soft_timeouts);
  cloud_scs_->QuarantineForNoResponse(*sc);
  EmitAttemptExhausted(sc);
  ActivateFollowing(1);
}

void CloudRequest::OnChannelChanged(CloudServerConnection* sc) {
  auto it = server_requests_.find(sc);
  if (it == server_requests_.end()) {
    return;
  }
  auto& sr = it->second;
  auto const action = sr.exec.OnChannelChanged(exec_policy_);
  if (action ==
      CloudRequestServerExecState::ChannelChangedAction::kIgnore) {
    return;
  }
  if (action ==
      CloudRequestServerExecState::ChannelChangedAction::kExhaust) {
    ExhaustServerNoResponse(sc, sr);
    EnqueuePump();
    return;
  }
  // Exactly one LaunchAttempt per channel-changed event.
  LaunchAttempt(sc, sr);
  EnqueuePump();
}

void CloudRequest::OnWriteFailed(CloudServerConnection* sc) {
  auto it = server_requests_.find(sc);
  if (it == server_requests_.end()) {
    return;
  }
  auto& sr = it->second;
  if (sr.exec.IsTerminal()) {
    return;
  }
  // WriteAction::kFail: treat as send-path failure using soft retry budget
  // (no Restream). Hard LinkError quarantine is owned by CloudServerConnections
  // stream/error subscriptions — ExhaustServerNoResponse may also quarantine
  // after budget exhaustion if the write failures never produced a response.
  bool const first_soft_miss = !sr.exec.first_soft_miss_seen;
  auto const action = sr.exec.OnSoftTimeout(exec_policy_);
  if (first_soft_miss && exec_policy_.hedge_next_servers > 0) {
    ActivateFollowing(exec_policy_.hedge_next_servers, /*as_hedge=*/true, sc);
  }
  if (action == CloudRequestServerExecState::SoftTimeoutAction::kRetry) {
    LaunchAttempt(sc, sr);
  } else if (action ==
             CloudRequestServerExecState::SoftTimeoutAction::kExhaust) {
    ExhaustServerNoResponse(sc, sr);
  }
  EnqueuePump();
}

void CloudRequest::ServersUpdated() {
  RebuildCandidates();
  bool any_activated = false;
  for (auto const& [sc, sr] : server_requests_) {
    static_cast<void>(sc);
    if (sr.exec.activated && !sr.exec.IsTerminal()) {
      any_activated = true;
      break;
    }
  }
  if (!any_activated) {
    ActivateInitial();
  } else {
    ActivateFollowing(1);
  }
  EnqueuePump();
}

void CloudRequest::EnqueuePump() {
  if (task_sub_) {
    return;
  }
  task_sub_ = ae_context_.scheduler().Task([this]() {
    task_sub_.Reset();
    Pump();
  });
}

void CloudRequest::Pump() {
  bool any_open = false;
  bool any_succeeded = false;
  for (auto const& [sc, sr] : server_requests_) {
    static_cast<void>(sc);
    if (sr.exec.succeeded) {
      any_succeeded = true;
    } else if (sr.exec.activated && !sr.exec.IsTerminal()) {
      any_open = true;
    } else if (!sr.exec.activated && !sr.exec.IsTerminal()) {
      any_open = true;
    }
  }
  if (activate_cursor_ < candidates_.size()) {
    any_open = true;
  }

  if (!server_requests_.empty() &&
      CloudRequestShouldFailAll(any_open, any_succeeded) &&
      activate_cursor_ >= candidates_.size()) {
    AE_CLOUD_REQ_ERROR("All server requests exhausted, failing");
    Failed();
  }
}

void CloudRequest::Finish() {
  server_changed_sub_.Reset();
  task_sub_.Reset();
  server_requests_.clear();
  candidates_.clear();
  Action::Finish();
}

}  // namespace ae
