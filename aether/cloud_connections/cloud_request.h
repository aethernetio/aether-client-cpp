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

#include <map>

#include "aether/common.h"
#include "aether/ae_context.h"
#include "aether/actions/action2_.h"
#include "aether/cloud_connections/request_policy.h"
#include "aether/cloud_connections/cloud_callbacks.h"
#include "aether/cloud_connections/cloud_server_connections.h"

namespace ae {
class CloudRequest {
  class EmptyConnectionsWA final : public WriteAction {
   public:
    explicit EmptyConnectionsWA(AeContext const& ae_context) {
      ae_context.scheduler().Task(
          [&]() { WriteAction::SetStatus(WriteAction::Status::kFail); });
    }
  };

  class ReplicaWA final : public WriteAction {
   public:
    explicit ReplicaWA(std::vector<WriteAction*>&& swas) noexcept;

    void Stop() noexcept override;

   private:
    std::vector<WriteAction*> swas_;
    std::size_t failed_actions_{};
    std::size_t stopped_actions_{};
    MultiSubscription subs_;
  };

 public:
  CloudRequest(AeContext const& ae_context, CloudServerConnections& connection);

  WriteAction& CallApi(
      AuthApiCaller const& api_caller,
      RequestPolicy::Variant policy = RequestPolicy::MainServer{});

 private:
  WriteAction& EmptyWriteAction();
  WriteAction& ReplicaWriteAction(std::vector<WriteAction*>&& swas);

  AeContext ae_context_;
  CloudServerConnections* connection_;
  std::optional<EmptyConnectionsWA> empty_wa_;
  std::vector<ReplicaWA> replica_was_;
};

/**
 * \brief Makes request according to the request policy.
 * If request fails or times out, it will restream the cloud connection and
 * retrie until max_attempts is reached.
 * ResponseListener listener must subscribe to client_api and handle the
 * response. On success, listener must call CloudRequestAction::Succeeded(). On
 * failure, listener must call CloudRequestAction::Failed().
 */
class CloudRequestAction final : public a2::Action {
  struct ServerRequest {
    MultiSubscription state_subs;
    bool is_active{false};
  };

 public:
  using SuccessEvent = Event<void()>;
  using FailureEvent = Event<void()>;

  CloudRequestAction(AeContext const& ae_context, AuthApiCaller&& api_caller,
                     ClientResponseListener&& listener,
                     CloudServerConnections& cloud_server_connections,
                     RequestPolicy::Variant policy);

  CloudRequestAction(AeContext const& ae_context, AuthApiRequest&& api_request,
                     CloudServerConnections& cloud_server_connections,
                     RequestPolicy::Variant policy);

  AE_CLASS_NO_COPY_MOVE(CloudRequestAction)

  void Succeeded();
  void Failed();

  SuccessEvent::Subscriber success_event();
  FailureEvent::Subscriber failure_event();

 private:
  void MakeRequest();
  void MakeServerRequest(CloudServerConnection* sc, ServerRequest& sr);

  void ServersUpdated();

  void RemoveRequest(CloudServerConnection* server_connection);
  void EnqueueMakeRequest();

  void Finish();

  AeContext ae_context_;
  std::variant<AuthApiCaller, AuthApiRequest> request_;
  ClientResponseListener listener_;
  CloudServerConnections* cloud_sc_;
  RequestPolicy::Variant policy_;
  std::optional<TaskSubscription> task_sub_;

  Subscription swa_sub_;
  Subscription server_changed_sub_;
  SuccessEvent success_event_;
  FailureEvent failure_event_;

  std::map<CloudServerConnection*, ServerRequest> server_requests_;
};

}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_CLOUD_REQUEST_H_
