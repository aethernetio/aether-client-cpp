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
#include "aether/actions/action.h"
#include "aether/types/state_machine.h"
#include "aether/cloud_connections/request_policy.h"
#include "aether/cloud_connections/cloud_callbacks.h"
#include "aether/cloud_connections/cloud_server_connections.h"
#include "aether/cloud_connections/cloud_subscription.h"

namespace ae {
class CloudRequest {
 public:
  static ActionPtr<WriteAction> CallApi(
      AuthApiCaller const& api_caller, CloudServerConnections& connection,
      RequestPolicy::Variant policy = RequestPolicy::MainServer{});
};

/**
 * \brief Makes request according to the request policy.
 * If request fails or times out, it will restream the cloud connection and
 * retire until max_attempts is reached.
 * ResponseListener listener must subscribe to client_api and handle the
 * response. On success, listener must call CloudRequestAction::Succeeded(). On
 * failure, listener must call CloudRequestAction::Failed().
 */
class CloudRequestAction final : public Action<CloudRequestAction> {
  enum class State : std::uint8_t {
    kMakeRequest,
    kWait,
    kResult,
    kFailed,
    kStopped,
  };

  struct ServerRequest {
    MultiSubscription state_subs;
    bool is_active{false};
  };

 public:
  CloudRequestAction(ActionContext action_context, AuthApiCaller&& api_caller,
                     ClientResponseListener&& listener,
                     CloudServerConnections& cloud_server_connections,
                     RequestPolicy::Variant policy);

  CloudRequestAction(ActionContext action_context, AuthApiRequest&& api_request,
                     CloudServerConnections& cloud_server_connections,
                     RequestPolicy::Variant policy);

  AE_CLASS_NO_COPY_MOVE(CloudRequestAction)

  UpdateStatus Update(TimePoint current_time);
  void Stop();

  void Succeeded();
  void Failed();

 private:
  void MakeRequest(TimePoint current_time);

  void MakeRequest(TimePoint current_time,
                   CloudServerConnection* server_connection);

  void ServersUpdated();

  ServerRequest* SaveRequest(CloudServerConnection* server_connection);
  void RemoveRequest(CloudServerConnection* server_connection);

  std::variant<AuthApiCaller, AuthApiRequest> request_;
  ClientResponseListener listener_;
  CloudServerConnections* cloud_sc_;
  RequestPolicy::Variant policy_;

  CloudSubscription cloud_sub_;
  StateMachine<State> state_;
  Subscription swa_sub_;
  Subscription server_changed_sub_;

  std::map<CloudServerConnection*, ServerRequest> server_requests_;
};

}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_CLOUD_REQUEST_H_
