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
#include "aether/actions/action.h"
#include "aether/cloud_connections/request_policy.h"
#include "aether/cloud_connections/cloud_callbacks.h"
#include "aether/cloud_connections/cloud_server_connections.h"

namespace ae {
/**
 * \brief Makes request according to the request policy.
 * If request fails or times out, it will restream the cloud connection and
 * retry on different channels. When a server exhausts its retry budget,
 * it moves on to the next server in the list.
 * ResponseSubscriber must subscribe to client_api and handle the
 * response. On success, listener must call CloudRequest::Succeeded(). On
 * failure, listener must call CloudRequest::Failed().
 */
class CloudRequest final : public Action {
  struct ServerRequest {
    MultiSubscription state_subs;
    TaskSubscription timeout_sub;
    std::size_t retry_count{0};
    bool exhausted{false};
  };

 public:
  static constexpr std::size_t kDefaultMaxRetries = 5;
  static constexpr Duration kDefaultRequestTimeout =
      std::chrono::milliseconds{AE_CLOUD_REQUEST_TIMEOUT_MS};

  using ResultEvent = Event<void(bool)>;

  CloudRequest(AeContext const& ae_context, ApiCallWithListener&& api_call,
               CloudServerConnections& cloud_server_connections,
               RequestPolicy::Variant policy,
               std::size_t max_retries = kDefaultMaxRetries,
               Duration request_timeout = kDefaultRequestTimeout);

  CloudRequest(AeContext const& ae_context, ApiRequestHandler&& api_request,
               CloudServerConnections& cloud_server_connections,
               RequestPolicy::Variant policy,
               std::size_t max_retries = kDefaultMaxRetries,
               Duration request_timeout = kDefaultRequestTimeout);

  AE_CLASS_NO_COPY_MOVE(CloudRequest)

  void Succeeded();
  void Failed();

  ResultEvent::Subscriber result_event();

 private:
  void MakeRequest();
  void MakeServerRequest(CloudServerConnection* sc, ServerRequest& sr);
  void PrefillServerRequests();

  void ServersUpdated();
  void OnChannelChanged(CloudServerConnection* sc);
  void OnServerRequestTimeout(CloudServerConnection* sc);
  void OnWriteFailed(CloudServerConnection* sc);

  void RemoveRequest(CloudServerConnection* server_connection);
  void EnqueueMakeRequest();

  void Finish();

  AeContext ae_context_;
  std::variant<ApiCallWithListener, ApiRequestHandler> request_;
  CloudServerConnections* cloud_scs_;
  RequestPolicy::Variant policy_;
  std::size_t max_retries_;
  Duration request_timeout_;
  TaskSubscription task_sub_;

  Subscription swa_sub_;
  Subscription server_changed_sub_;
  ResultEvent result_event_;

  std::map<CloudServerConnection*, ServerRequest> server_requests_;
};

}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_CLOUD_REQUEST_H_
