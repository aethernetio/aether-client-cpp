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

#ifndef AETHER_CLOUD_CONNECTIONS_CLOUD_SUBSCRIPTION_H_
#define AETHER_CLOUD_CONNECTIONS_CLOUD_SUBSCRIPTION_H_

#include "aether/common.h"

#include "aether/cloud_connections/request_policy.h"
#include "aether/cloud_connections/cloud_callbacks.h"
#include "aether/cloud_connections/cloud_server_connections.h"

namespace ae {
class CloudSubscription {
 public:
  CloudSubscription() = default;
  CloudSubscription(
      ClientListener subscriber, CloudServerConnections& cloud_connection,
      RequestPolicy::Variant request_policy = RequestPolicy::MainServer{});

  CloudSubscription(CloudSubscription&& other) noexcept;
  CloudSubscription& operator=(CloudSubscription&& other) noexcept;

  AE_CLASS_NO_COPY(CloudSubscription)

  void Reset();

 private:
  void ServersUpdate();

  CloudServerConnections* cloud_connection_;
  ClientListener subscriber_;
  RequestPolicy::Variant request_policy_;
  Subscription server_update_sub_;
  MultiSubscription subscriptions_;
};
}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_CLOUD_SUBSCRIPTION_H_
