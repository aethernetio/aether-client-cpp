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

#include "aether/cloud_connections/cloud_server_connections.h"

#include "aether/server.h"
#include "aether/server_connections/server_connection.h"

#include "aether/tele/tele.h"

namespace ae {

CloudServerConnections::CloudServerConnections(
    ActionContext action_context, ClientConnectionManager& connection_manager,
    std::size_t max_connections)
    : action_context_{action_context},
      connection_manager_{&connection_manager},
      max_connections_{max_connections} {
  InitServers();
}

CloudServerConnections::ServersUpdate::Subscriber
CloudServerConnections::servers_update_event() {
  return servers_update_event_;
}

std::vector<ServerConnection*> const& CloudServerConnections::servers() const {
  return selected_servers_;
}

std::size_t CloudServerConnections::count_connections() const {
  return selected_servers_.size();
}

std::size_t CloudServerConnections::max_connections() const {
  return max_connections_;
}

void CloudServerConnections::Restream() {
  // restream all servers
  for (auto const& server : selected_servers_) {
    server->Restream();
  }
}

void CloudServerConnections::InitServers() {
  AE_TELED_DEBUG("Init servers");
  reselect_servers_action_ = OwnActionPtr<ReselectServers>{action_context_};

  reselect_servers_action_->StatusEvent().Subscribe(
      OnResult{[this]() { InitServers(); }});

  SelectServers();
}

void CloudServerConnections::SelectServers() {
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
              // Quarantine servers are always at the end
              if (left->quarantine()) {
                return false;
              }
              if (right->quarantine()) {
                return true;
              }
              // TODO: add sorting by external policy
              // Sort by priority
              return left->priority() < right->priority();
            });

  auto select_count = std::min(servers.size(), max_connections_);

  auto get_ids = [&](auto const& ss) {
    std::vector<ServerId> sids;
    sids.reserve(ss.size());
    for (auto const* server : ss) {
      sids.push_back(server->server()->server_id);
    }
    return sids;
  };
  AE_TELED_DEBUG("Select servers count {} from sids [{}]", select_count,
                 get_ids(servers));
  selected_servers_.clear();
  selected_servers_.reserve(select_count);

  std::size_t i = 0;
  // Select required count of servers and begin connection.
  for (; i < select_count; i++) {
    auto* serv = servers[i];
    if (serv->quarantine()) {
      break;
    }
    selected_servers_.emplace_back(serv);
    serv->BeginConnection(i);
    SubscribeToServerState(serv);
  }
  // Also explicitly end the other servers connection.
  for (; i < servers.size(); i++) {
    servers[i]->EndConnection(i);
  }

  AE_TELED_DEBUG("Selected servers [{}]", get_ids(selected_servers_));
  servers_update_event_.Emit();
}

void CloudServerConnections::SubscribeToServerState(
    ServerConnection* server_connection) {
  auto bad_server = [this, server_connection]() {
    // TODO: add the policy how to change the server priority on failure
    // put server in quarantine and make it the least prioritized
    auto new_priority = server_connection->priority() +
                        connection_manager_->server_connections().size();
    server_connection->EndConnection(new_priority);
    QuarantineTimer(server_connection);
    UnselectServer(server_connection);
    reselect_servers_action_->Notify();
  };

  auto* conn = server_connection->ClientConnection();
  if (conn == nullptr) {
    bad_server();
    return;
  }
  // any link error leads to reselect the servers
  if (conn->stream_info().link_state == LinkState::kLinkError) {
    bad_server();
    return;
  }

  server_state_subs_ += conn->stream_update_event().Subscribe(
      [bad_server, conn, sid{server_connection->server()->server_id}]() {
        AE_TELED_DEBUG("Server connection id {}, link state {}", sid,
                       conn->stream_info().link_state);
        // any link error leads to reselect the servers
        if (conn->stream_info().link_state == LinkState::kLinkError) {
          bad_server();
        }
      });
}

void CloudServerConnections::UnselectServer(
    ServerConnection* server_connection) {
  auto it = std::find(std::begin(selected_servers_),
                      std::end(selected_servers_), server_connection);
  if (it == std::end(selected_servers_)) {
    return;
  }
  it = selected_servers_.erase(it);
  auto dist = std::distance(std::begin(selected_servers_), it);
  auto new_priority = static_cast<std::size_t>(dist);
  // update priorities for remaining servers
  for (; it != std::end(selected_servers_); it++) {
    (*it)->BeginConnection(new_priority++);
  }
  AE_TELED_DEBUG("Servers unselected, remaining count {}",
                 selected_servers_.size());
}

void CloudServerConnections::QuarantineTimer(
    ServerConnection* server_connection) {
  AE_TELED_DEBUG("Set server {} to qurantine",
                 server_connection->server()->server_id);
  if (!quarantine_timer_ || quarantine_timer_->IsFinished()) {
    // TODO: add config for server quarantine duration
    quarantine_timer_ =
        OwnActionPtr<TimerAction>{action_context_, std::chrono::seconds{10}};
  }
  server_connection->quarantine(true);
  quarantine_subs_ += quarantine_timer_->StatusEvent().Subscribe(  // ~(^.^)~
      OnResult{
          [this, sc{server_connection}]() {
            AE_TELED_DEBUG("Release from quarantine server {}",
                           sc->server()->server_id);
            sc->quarantine(false);
            // update selected servers if required
            if (selected_servers_.size() < max_connections_) {
              reselect_servers_action_->Notify();
            }
          },
      });
}

}  // namespace ae
