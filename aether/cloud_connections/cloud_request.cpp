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
  AE_CLOUD_REQ_DEBUG(
      "CLOUD_REQUEST_START percentile={} factor_permille={} retry_count={} "
      "hedge_next_servers={}",
      exec_policy_.response_percentile, exec_policy_.timeout_factor_permille,
      exec_policy_.retry_count, exec_policy_.hedge_next_servers);
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
  AE_CLOUD_REQ_DEBUG(
      "CLOUD_REQUEST_START percentile={} factor_permille={} retry_count={} "
      "hedge_next_servers={}",
      exec_policy_.response_percentile, exec_policy_.timeout_factor_permille,
      exec_policy_.retry_count, exec_policy_.hedge_next_servers);
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

void CloudRequest::SucceedAttempt(CloudServerConnection* sc) {
  auto it = server_requests_.find(sc);
  if (it == server_requests_.end()) {
    return;
  }
  auto& sr = it->second;
  if (sr.exec.succeeded || sr.exec.exhausted) {
    return;
  }
  AE_CLOUD_REQ_DEBUG("SERVER_ATTEMPT_SUCCESS server_id={}", sc->server_id());
  // Cancel future soft timers; keep response_subs so in-flight late responses
  // can still be observed by the listener if needed, but mark success first.
  for (auto& attempt : sr.attempts) {
    attempt.timeout_sub.Reset();
  }
  sr.exec.MarkSucceeded();
  // Sequential hedge=0 style: after success, activate the next candidate if
  // RequestPolicy still requires more servers.
  ActivateFollowing(1);
  EnqueuePump();
}

bool CloudRequest::FailAttempt(CloudServerConnection* sc) {
  auto it = server_requests_.find(sc);
  if (it == server_requests_.end()) {
    return false;
  }
  auto& sr = it->second;
  if (sr.exec.succeeded || sr.exec.exhausted) {
    return false;
  }
  // Treat application-reported failure like a soft miss for budget purposes:
  // do not Restream; retry if budget remains; otherwise quarantine.
  bool const first_soft_miss = !sr.exec.first_soft_miss_seen;
  auto const action = sr.exec.OnSoftTimeout(exec_policy_);
  if (first_soft_miss && exec_policy_.hedge_next_servers > 0) {
    ActivateFollowing(exec_policy_.hedge_next_servers, /*as_hedge=*/true, sc);
  }
  if (action == CloudRequestServerExecState::SoftTimeoutAction::kRetry) {
    LaunchAttempt(sc, sr);
    EnqueuePump();
    return false;
  }
  if (action == CloudRequestServerExecState::SoftTimeoutAction::kExhaust) {
    ExhaustServerNoResponse(sc, sr);
    EnqueuePump();
    return true;
  }
  EnqueuePump();
  return sr.exec.exhausted;
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
  // Preserve activation cursor semantics: append newly eligible servers after
  // existing candidate order; keep already-known pointers stable.
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
    if (sr.exec.activated || sr.exec.succeeded || sr.exec.exhausted) {
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
  LaunchAttempt(sc, sr);
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
    // Hard unusable: exhaust and quarantine via existing health path if link
    // errors; for a missing connection treat as attempt failure.
    auto const action = sr.exec.OnSoftTimeout(exec_policy_);
    if (action == CloudRequestServerExecState::SoftTimeoutAction::kExhaust) {
      ExhaustServerNoResponse(sc, sr);
    } else if (action ==
               CloudRequestServerExecState::SoftTimeoutAction::kRetry) {
      // Will retry on next pump when connection appears.
      sr.exec.attempts_started =
          static_cast<std::uint8_t>(sr.exec.attempts_started - 1);
    }
    return;
  }

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

  // Write failure is a hard-ish send problem — count toward budget without
  // Restream from soft timeout path.
  attempt.subs += swa.status_event().Subscribe([this, sc](auto status) {
    if (status == WriteAction::Status::kFail) {
      AE_CLOUD_REQ_WARNING("Request write error server {}", sc->server_id());
      OnWriteFailed(sc);
    }
  });
  attempt.subs +=
      conn->server_connection().channel_changed_event().Subscribe([this, sc]() {
        AE_CLOUD_REQ_WARNING("Request server channel changed {}",
                             sc->server_id());
        OnChannelChanged(sc);
      });

  if (std::holds_alternative<ApiCallWithListener>(request_)) {
    auto& listener = std::get<ApiCallWithListener>(request_).listener;
    if (listener) {
      // Keep listener subscriptions durable so late responses from earlier
      // attempts are not destroyed when a retry is launched.
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
  if (sr.exec.succeeded || sr.exec.exhausted) {
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
  if (sr.exec.succeeded) {
    return;
  }
  sr.exec.MarkExhausted();
  for (auto& attempt : sr.attempts) {
    attempt.timeout_sub.Reset();
  }
  AE_CLOUD_REQ_WARNING(
      "SERVER_RETRY_EXHAUSTED server_id={} attempts={} -> "
      "SERVER_QUARANTINE_NO_RESPONSE",
      sc->server_id(), sr.exec.attempts_started);
  cloud_scs_->QuarantineForNoResponse(*sc);
  EmitAttemptExhausted(sc);
  // After quarantine/reconcile, ServersUpdated may add a replacement — also
  // advance sequential activation for remaining required candidates.
  ActivateFollowing(1);
}

void CloudRequest::OnChannelChanged(CloudServerConnection* sc) {
  auto it = server_requests_.find(sc);
  if (it == server_requests_.end()) {
    return;
  }
  auto& sr = it->second;
  if (sr.exec.succeeded || sr.exec.exhausted || !sr.exec.activated) {
    return;
  }
  // Channel change is infrastructure: re-send without Restream-from-soft-timeout,
  // using remaining attempt budget if available.
  if (!sr.exec.CanStartAttempt(exec_policy_)) {
    ExhaustServerNoResponse(sc, sr);
    EnqueuePump();
    return;
  }
  LaunchAttempt(sc, sr);
  EnqueuePump();
}

void CloudRequest::OnWriteFailed(CloudServerConnection* sc) {
  auto it = server_requests_.find(sc);
  if (it == server_requests_.end()) {
    return;
  }
  auto& sr = it->second;
  if (sr.exec.succeeded || sr.exec.exhausted) {
    return;
  }
  // Write failure: do not soft-Restream; apply retry budget. Hard LinkError
  // quarantine remains on the connection health path.
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
  // If nothing is active yet, start the first candidate. Otherwise activate
  // newly appended replacements only when sequential policy needs them or
  // hedge already opened the window — ActivateFollowing(1) after exhaustion
  // handles the common replacement case.
  bool any_activated = false;
  for (auto const& [sc, sr] : server_requests_) {
    static_cast<void>(sc);
    if (sr.exec.activated && !sr.exec.exhausted && !sr.exec.succeeded) {
      any_activated = true;
      break;
    }
  }
  if (!any_activated) {
    ActivateInitial();
  } else {
    // Ensure replacements beyond the cursor can be pulled in for All.
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
    } else if (sr.exec.activated && !sr.exec.exhausted) {
      any_open = true;
    } else if (!sr.exec.activated && !sr.exec.exhausted) {
      // Candidate not yet activated — still open for sequential/hedge.
      any_open = true;
    }
  }
  // Candidates reserved but not activated still count as open work.
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
