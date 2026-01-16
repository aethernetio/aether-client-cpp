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

std::vector<CloudServerConnection*> const& CloudServerConnections::servers()
    const {
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

  auto server_candidates = ServerCandidates();
  // reselect servers if required
  if (selected_servers_.size() >=
      std::min(server_candidates.size(), max_connections_)) {
    return;
  }
  SelectServers(server_candidates);
}

void CloudServerConnections::SelectServers(
    std::vector<CloudServerConnection*> const& servers) {
  auto select_count = std::min(servers.size(), max_connections_);

  auto get_ids = [&](auto const& ss) {
    std::vector<ServerId> sids;
    sids.reserve(ss.size());
    for (auto const* server : ss) {
      sids.emplace_back(server->server()->server_id);
    }
    return sids;
  };

  AE_TELED_DEBUG("Select servers count {} from sids [{}]", select_count,
                 get_ids(servers));

  selected_servers_.clear();
  selected_servers_.reserve(select_count);

  std::size_t i = 0;
  // Select required count of servers.
  for (; i < select_count; i++) {
    auto* serv = servers[i];
    selected_servers_.emplace_back(serv);
    // update server priority and if it begins new connection update
    // subscriptions
    if (serv->BeginConnection(i)) {
      SubscribeToServerState(*serv);
    }
  }
  // Also explicitly end the other servers connection.
  for (; i < servers.size(); i++) {
    servers[i]->EndConnection(i);
  }

  AE_TELED_DEBUG("Selected servers [{}]", get_ids(selected_servers_));
  servers_update_event_.Emit();
}

void CloudServerConnections::SubscribeToServerState(
    CloudServerConnection& server_connection) {
  auto bad_server = [this, sc{&server_connection}]() {
    // TODO: add the policy how to change the server priority on failure
    // put server in quarantine and make it the least prioritized
    auto new_priority =
        sc->priority() + connection_manager_->server_connections().size();
    sc->EndConnection(new_priority);
    QuarantineTimer(*sc);
    UnselectServer(*sc);
    reselect_servers_action_->Notify();
  };

  auto* conn = server_connection.client_connection();
  if (conn == nullptr) {
    bad_server();
    return;
  }

  auto if_bad_connection = [bad_server, conn,
                            sid{server_connection.server()->server_id}]() {
    AE_TELED_DEBUG("Server connection id {}, link state {}", sid,
                   conn->stream_info().link_state);
    if (conn->stream_info().link_state == LinkState::kLinkError) {
      bad_server();
      return true;
    }
    return false;
  };

  // any link error leads to reselect the servers
  if (if_bad_connection()) {
    return;
  }
  server_state_subs_[reinterpret_cast<std::uintptr_t>(&server_connection)] =
      conn->stream_update_event().Subscribe(if_bad_connection);
}

void CloudServerConnections::UnselectServer(
    CloudServerConnection& server_connection) {
  auto it = std::find(std::begin(selected_servers_),
                      std::end(selected_servers_), &server_connection);
  if (it == std::end(selected_servers_)) {
    return;
  }
  it = selected_servers_.erase(it);
  AE_TELED_DEBUG("Servers unselected, remaining count {}",
                 selected_servers_.size());
}

void CloudServerConnections::QuarantineTimer(
    CloudServerConnection& server_connection) {
  AE_TELED_DEBUG("Set server {} to quarantine",
                 server_connection.server()->server_id);
  if (!quarantine_timer_ || quarantine_timer_->IsFinished()) {
    static constexpr Duration kQuarantineDuration =
        std::chrono::milliseconds{AE_CLOUD_SERVER_QUARANTINE_TIME_MS};
    quarantine_timer_ =
        OwnActionPtr<TimerAction>{action_context_, kQuarantineDuration};
  }
  server_connection.quarantine(true);
  quarantine_timer_->StatusEvent().Subscribe(  // ~(^.^)~
      OnResult{
          [this, sc{&server_connection}]() {
            AE_TELED_DEBUG("Release from quarantine server {}",
                           sc->server()->server_id);
            sc->quarantine(false);
            // update selected servers
            reselect_servers_action_->Notify();
          },
      });
}

std::vector<CloudServerConnection*> CloudServerConnections::ServerCandidates() {
  std::vector<CloudServerConnection*> servers;
  servers.reserve(connection_manager_->server_connections().size());
  for (auto& s : connection_manager_->server_connections()) {
    if (s.quarantine()) {
      continue;
    }
    servers.emplace_back(&s);
  }

  // sort list of servers by it's priorities
  std::sort(std::begin(servers), std::end(servers),
            [](auto const* left, auto const* right) {
              // TODO: add sorting by external policy
              // Sort by priority
              // The default priority is 0
              // but it's changed lately after servers selected
              return left->priority() < right->priority();
            });

  return servers;
}

}  // namespace ae
