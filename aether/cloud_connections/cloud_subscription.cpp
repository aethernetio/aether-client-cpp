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

#include "aether/cloud_connections/cloud_subscription.h"

#include "aether/cloud_connections/cloud_visit.h"

namespace ae {
CloudSubscription::CloudSubscription(ClientListener subscriber,
                                     CloudServerConnections& cloud_connection,
                                     RequestPolicy::Variant request_policy)
    : cloud_connection_{&cloud_connection},
      subscriber_{std::move(subscriber)},
      request_policy_{request_policy} {
  server_update_sub_ = cloud_connection_->servers_update_event().Subscribe(
      MethodPtr<&CloudSubscription::ServersUpdate>{this});
  ServersUpdate();
}

CloudSubscription::CloudSubscription(CloudSubscription&& other) noexcept
    : cloud_connection_{other.cloud_connection_},
      subscriber_{std::move(other.subscriber_)},
      request_policy_{other.request_policy_},
      subscriptions_{std::move(other.subscriptions_)} {
  if (cloud_connection_ != nullptr) {
    server_update_sub_ = cloud_connection_->servers_update_event().Subscribe(
        MethodPtr<&CloudSubscription::ServersUpdate>{this});
  }
}

CloudSubscription& CloudSubscription::operator=(
    CloudSubscription&& other) noexcept {
  if (this != &other) {
    cloud_connection_ = other.cloud_connection_;
    subscriber_ = std::move(other.subscriber_);
    request_policy_ = other.request_policy_;
    subscriptions_ = std::move(other.subscriptions_);
    if (cloud_connection_ != nullptr) {
      server_update_sub_ = cloud_connection_->servers_update_event().Subscribe(
          MethodPtr<&CloudSubscription::ServersUpdate>{this});
    }
  }
  return *this;
}

void CloudSubscription::ServersUpdate() {
  // clean old subscriptions and make new
  subscriptions_.Reset();
  CloudVisit::Visit(
      [&](auto* sc) {
        auto* conn = sc->client_connection();
        assert((conn != nullptr) && "ClientConnection is null");
        subscriptions_ += subscriber_(conn->client_safe_api(), sc);
      },
      *cloud_connection_, request_policy_);
}

void CloudSubscription::Reset() {
  server_update_sub_.Reset();
  subscriptions_.Reset();
}
}  // namespace ae
