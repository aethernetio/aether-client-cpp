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
#ifndef AETHER_CLOUD_CONNECTIONS_CLOUD_REQUEST_H_
#define AETHER_CLOUD_CONNECTIONS_CLOUD_REQUEST_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "aether/common.h"
#include "aether/ae_context.h"
#include "aether/actions/action.h"
#include "aether/cloud_connections/cloud_request_execution_policy.h"
#include "aether/cloud_connections/request_policy.h"
#include "aether/cloud_connections/cloud_callbacks.h"
#include "aether/cloud_connections/cloud_server_connections.h"
#include "aether/events/event_subscription.h"
#include "aether/events/multi_subscription.h"

namespace ae {
/**
 * \brief Makes request according to RequestPolicy (candidate set) and
 * CloudRequestExecutionPolicy (soft timeout / retry / hedge / quarantine).
 *
 * Soft response timeout does NOT Restream or quarantine. Quarantine for
 * no-response happens only after the per-server retry budget is exhausted.
 * Late valid responses from earlier attempts are accepted.
 *
 * Authenticated API-level errors (CompleteAttemptWithRemoteError) prove the
 * server is alive and must not quarantine via the no-response path.
 *
 * ResponseSubscriber / ApiRequestHandler must handle responses. On whole
 * request success, call CloudRequest::Succeeded(); on whole failure,
 * CloudRequest::Failed(). Per-server: SucceedAttempt /
 * CompleteAttemptWithRemoteError.
 */
// Compatibility alias for older unit helpers.
using CloudRequestAttemptState = CloudRequestServerExecState;

class CloudRequest final : public Action {
  struct AttemptState {
    // Write-status subscription for this attempt only.
    MultiSubscription write_subs;
    TaskSubscription timeout_sub;
    std::uint8_t attempt_index{0};
    bool timed_out{false};
  };

  struct ServerRequest {
    CloudRequestServerExecState exec{};
    // Durable across attempts so late responses remain deliverable.
    MultiSubscription response_subs;
    // One channel_changed subscription for the whole server lifetime in this
    // CloudRequest ? not per attempt.
    Subscription channel_changed_sub;
    std::vector<AttemptState> attempts;
  };

 public:
  using ResultEvent = Event<void(bool)>;
  using AttemptExhaustedEvent = Event<void(CloudServerConnection*)>;

  CloudRequest(AeContext const& ae_context, ApiCallWithListener&& api_call,
               CloudServerConnections& cloud_server_connections,
               RequestPolicy::Variant policy,
               CloudRequestExecutionPolicy exec_policy =
                   CloudRequestExecutionPolicy::Default());

  CloudRequest(AeContext const& ae_context, ApiRequestHandler&& api_request,
               CloudServerConnections& cloud_server_connections,
               RequestPolicy::Variant policy,
               CloudRequestExecutionPolicy exec_policy =
                   CloudRequestExecutionPolicy::Default());

  AE_CLASS_NO_COPY_MOVE(CloudRequest)

  void Succeeded();
  void Failed();
  // Per-server success: accept late responses, cancel future retries, no
  // Restream / quarantine.
  void SucceedAttempt(CloudServerConnection* sc);
  // Valid authenticated response with API-level failure. Server is alive:
  // no soft-timeout retry budget, no no-response quarantine.
  void CompleteAttemptWithRemoteError(CloudServerConnection* sc);

  CloudRequestExecutionPolicy const& execution_policy() const noexcept {
    return exec_policy_;
  }

  ResultEvent::Subscriber result_event();
  AttemptExhaustedEvent::Subscriber attempt_exhausted_event();

 private:
  void RebuildCandidates();
  void ActivateInitial();
  void ActivateFollowing(std::uint8_t count, bool as_hedge = false,
                         CloudServerConnection* source = nullptr);
  void ActivateServer(CloudServerConnection* sc);
  void EnsureChannelChangedSubscription(CloudServerConnection* sc,
                                        ServerRequest& sr);
  void LaunchAttempt(CloudServerConnection* sc, ServerRequest& sr);
  void StopServerTimers(ServerRequest& sr);

  Duration SoftTimeoutFor(CloudServerConnection* sc) const;
  void OnSoftTimeout(CloudServerConnection* sc, std::uint8_t attempt_index);
  void ExhaustServerNoResponse(CloudServerConnection* sc, ServerRequest& sr);

  void ServersUpdated();
  void OnChannelChanged(CloudServerConnection* sc);
  void OnWriteFailed(CloudServerConnection* sc);

  void EnqueuePump();
  void Pump();
  void EmitAttemptExhausted(CloudServerConnection* sc);
  void Finish();

  AeContext ae_context_;
  std::variant<ApiCallWithListener, ApiRequestHandler> request_;
  CloudServerConnections* cloud_scs_;
  RequestPolicy::Variant policy_;
  // Snapshot at construction ? runtime policy changes do not affect this op.
  CloudRequestExecutionPolicy exec_policy_;

  TaskSubscription task_sub_;
  Subscription server_changed_sub_;
  ResultEvent result_event_;
  AttemptExhaustedEvent attempt_exhausted_event_;

  std::vector<CloudServerConnection*> candidates_;
  std::size_t activate_cursor_{0};
  std::map<CloudServerConnection*, ServerRequest> server_requests_;
};

}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_CLOUD_REQUEST_H_
