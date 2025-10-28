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

#include "aether/client_connections/cloud_connection.h"

#include <algorithm>

#include "aether/stream_api/stream_write_action.h"
#include "aether/server_connections/server_connection.h"

#include "aether/tele/tele.h"

namespace ae {
namespace cloud_connection_internal {
class ReplicaStreamWriteAction final : public StreamWriteAction {
 public:
  ReplicaStreamWriteAction(
      ActionContext action_context,
      std::vector<ActionPtr<StreamWriteAction>> replica_actions)
      : StreamWriteAction(action_context),
        replica_actions_(std::move(replica_actions)) {
    for (auto& action : replica_actions_) {
      // get the state from replicas by OR
      action->state().changed_event().Subscribe([this](auto state) {
        if (state_ < state) {
          state_ = state;
        }
      });
    }
  }

  void Stop() override {
    for (auto& action : replica_actions_) {
      action->Stop();
    }
  }

 private:
  std::vector<ActionPtr<StreamWriteAction>> replica_actions_;
};

class ReplicaSubscription {
 public:
  ReplicaSubscription(CloudConnection& cloud_connection,
                      CloudConnection::ClientApiSubscriber subscriber,
                      RequestPolicy::Variant request_policy)
      : cloud_connection_{&cloud_connection},
        subscriber_{std::move(subscriber)},
        request_policy_{request_policy},
        subscriptions_{MakeRcPtr<MultiSubscription>()} {}

  void operator()() {
    // clean old subscriptions and make new
    subscriptions_->Reset();
    cloud_connection_->VisitServers(
        [&](ServerConnection* server_connection) {
          subscriptions_->Push(subscriber_(
              server_connection->ClientConnection().client_safe_api(),
              server_connection));
        },
        request_policy_);
  }

 private:
  CloudConnection* cloud_connection_;
  CloudConnection::ClientApiSubscriber subscriber_;
  RequestPolicy::Variant request_policy_;
  // FIXME: RcPtr is used because Events requires copy_constructible for
  // std::function
  RcPtr<MultiSubscription> subscriptions_;
};

}  // namespace cloud_connection_internal

CloudConnection::CloudConnection(ActionContext action_context,
                                 ClientConnectionManager& connection_manager,
                                 std::size_t max_connections)
    : action_context_{action_context},
      connection_manager_{&connection_manager},
      max_connections_{max_connections} {
  InitServers();
}

ActionPtr<StreamWriteAction> CloudConnection::AuthorizedApiCall(
    AuthorizedApiCaller const& auth_api_caller, RequestPolicy::Variant policy) {
  std::vector<ActionPtr<StreamWriteAction>> write_actions;

  VisitServers(
      [&](ServerConnection* sc) {
        write_actions.push_back(
            sc->ClientConnection().AuthorizedApiCall(SubApi<AuthorizedApi>{
                [&](auto& api) { auth_api_caller(api, sc); }}));
      },
      policy);

  return ActionPtr<cloud_connection_internal::ReplicaStreamWriteAction>{
      action_context_, std::move(write_actions)};
}

Subscription CloudConnection::ClientApiSubscription(
    ClientApiSubscriber subscriber, RequestPolicy::Variant policy) {
  auto replica_subscriber = cloud_connection_internal::ReplicaSubscription{
      *this, std::move(subscriber), policy};
  // call the subscriber
  replica_subscriber();
  // call the subscriber on server update
  return servers_update_event().Subscribe(std::move(replica_subscriber));
}

void CloudConnection::VisitServers(ServerVisitor const& visitor,
                                   RequestPolicy::Variant policy) {
  std::visit([this, &visitor](auto p) { VisitServers(p, visitor); }, policy);
}

CloudConnection::ServersUpdate::Subscriber
CloudConnection::servers_update_event() {
  return servers_update_event_;
}

std::size_t CloudConnection::count_connections() const {
  return selected_servers_.size();
}

std::size_t CloudConnection::max_connections() const {
  return max_connections_;
}

void CloudConnection::Restream() {
  // restream all servers
  for (auto const& server : selected_servers_) {
    server->Restream();
  }
}

void CloudConnection::VisitServers(RequestPolicy::MainServer,
                                   ServerVisitor const& visitor) {
  auto* server_conn = selected_servers_.front();
  visitor(server_conn);
}

void CloudConnection::VisitServers(RequestPolicy::Priority priority,
                                   ServerVisitor const& visitor) {
  auto index = std::min(selected_servers_.size() - 1, priority.priority);
  auto* server_conn = selected_servers_[index];
  visitor(server_conn);
}

void CloudConnection::VisitServers(RequestPolicy::Replica replica,
                                   ServerVisitor const& visitor) {
  auto count = std::min(selected_servers_.size(), replica.count);
  for (std::size_t i = 0; i < count; ++i) {
    visitor(selected_servers_[i]);
  }
}

void CloudConnection::InitServers() {
  AE_TELED_DEBUG("Init servers");
  reselect_servers_action_ = OwnActionPtr<ReselectServers>{action_context_};

  reselect_servers_action_->StatusEvent().Subscribe(
      OnResult{[this]() { InitServers(); }});

  SelectServers();
}

void CloudConnection::SelectServers() {
  server_state_subs_.Reset();

  std::vector<ServerConnection*> servers;
  servers.reserve(connection_manager_->server_connections().size());
  std::transform(std::begin(connection_manager_->server_connections()),
                 std::end(connection_manager_->server_connections()),
                 std::back_inserter(servers),
                 [](auto& server_connection) { return &server_connection; });

  // sort list of servers by it's pririties
  std::sort(std::begin(servers), std::end(servers),
            [](auto const* left, auto const* right) {
              if (left->priority() != right->priority()) {
                return left->priority() < right->priority();
              }
              // TODO: add policy to sort if the priority is same
              return left->server()->server_id < right->server()->server_id;
            });
  auto select_count = std::min(servers.size(), max_connections_);

  selected_servers_.clear();
  selected_servers_.reserve(select_count);
  AE_TELED_DEBUG("Selected servers count {}", select_count);
  for (std::size_t i = 0; i < select_count; i++) {
    servers[i]->SetPriority(i);
    SubscribeToServerState(servers[i]);
    selected_servers_.emplace_back(servers[i]);
  }

  servers_update_event_.Emit();
}

void CloudConnection::SubscribeToServerState(
    ServerConnection* server_connection) {
  auto& connection = server_connection->ClientConnection();
  server_state_subs_.Push(connection.stream().stream_update_event().Subscribe(
      [this, server_connection, &connection]() {
        AE_TELED_DEBUG("Server connection id {}, link state {}",
                       server_connection->server()->server_id,
                       connection.stream().stream_info().link_state);
        // any link error leads to reselect the servers
        if (connection.stream().stream_info().link_state ==
            LinkState::kLinkError) {
          // TODO: add the policy how to change the server priority on failure
          // make server the least prioritized
          server_connection->SetPriority(
              server_connection->priority() +
              connection_manager_->server_connections().size());
          reselect_servers_action_->Notify();
        }
      }));
}

}  // namespace ae
