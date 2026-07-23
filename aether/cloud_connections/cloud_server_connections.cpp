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

#include <algorithm>

#include "aether/api_protocol/api_protocol.h"
#include "aether/server.h"
#include "aether/server_connections/server_connection.h"

#include "aether/tele.h"

namespace ae {

cloud_server_connections_internal::EmptyConnectionsWA::EmptyConnectionsWA(
    AeContext const& ae_context) {
  ae_context.scheduler().Task(
      [&]() { WriteAction::SetStatus(WriteAction::Status::kFail); });
}

cloud_server_connections_internal::ReplicaWA::ReplicaWA(
    std::vector<WriteAction*>&& swas) noexcept
    : swas_{std::move(swas)} {
  assert(!swas_.empty());
  for (auto* action : swas_) {
    subs_ +=
        action->status_event().Subscribe([this](WriteAction::Status status) {
          switch (status) {
            case WriteAction::Status::kSuccess:
              SetStatus(status);
              return;
            case WriteAction::Status::kFail:
              failed_actions_++;
              break;
            case WriteAction::Status::kStop:
              stopped_actions_++;
              break;
          }
          if ((failed_actions_ + stopped_actions_) >= swas_.size()) {
            SetStatus(status);
          }
        });
  }
}

void cloud_server_connections_internal::ReplicaWA::Stop() noexcept {
  for (auto* action : swas_) {
    action->Stop();
  }
}

CloudServerConnections::CloudServerConnections(
    AeContext const& ae_context, Ptr<Cloud> const& cloud,
    std::unique_ptr<IServerConnectionFactory> connection_factory,
    std::size_t max_connections)
    : ae_context_{ae_context},
      cloud_{cloud},
      connection_factory_{std::move(connection_factory)},
      max_connections_{max_connections} {
  InitServerConnections();
  InitServers();
}

CloudServerConnections::ServersUpdate::Subscriber
CloudServerConnections::servers_update_event() {
  return servers_update_event_;
}

std::vector<CloudServerConnection*> const&
CloudServerConnections::selected_servers() const {
  return selected_servers_;
}

std::vector<CloudServerConnection*> const& CloudServerConnections::servers() {
  return all_servers_;
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

void CloudServerConnections::InitServerConnections() {
  server_connections_.clear();
  auto cloud = cloud_.Lock();
  assert(cloud);
  server_connections_.reserve(cloud->servers().size());
  for (auto& server : cloud->servers()) {
    server_connections_.emplace_back(server.Load(), *connection_factory_);
  }
  all_servers_.clear();
  all_servers_.reserve(server_connections_.size());
  for (auto& s : server_connections_) {
    all_servers_.emplace_back(&s);
  }
}

void CloudServerConnections::InitServers() {
  AE_TELED_DEBUG("Init servers");
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

  auto get_ids = [&]([[maybe_unused]] auto const& ss) noexcept {
#if DEBUG
    std::vector<ServerId> sids;
    sids.reserve(ss.size());
    for (auto const* server : ss) {
      sids.emplace_back(server->server()->server_id);
    }
    return sids;
#else
    return "!not debug!";
#endif
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
    auto new_priority = sc->priority() + server_connections_.size();
    sc->EndConnection(new_priority);
    QuarantineTimer(*sc);
    UnselectServer(*sc);
    ReselectServers();
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

void CloudServerConnections::ReselectServers() {
  // reselct servers on next cycle
  if (defer_sub_) {
    return;
  }
  defer_sub_ = ae_context_.scheduler().Task([&]() {
    defer_sub_.Reset();
    InitServers();
  });
}

void CloudServerConnections::UnselectServer(
    CloudServerConnection& server_connection) {
  auto old_size = selected_servers_.size();
  std::erase(selected_servers_, &server_connection);
  if (selected_servers_.size() < old_size) {
    AE_TELED_DEBUG("Servers unselected, remaining count {}",
                   selected_servers_.size());
  }
}

void CloudServerConnections::QuarantineTimer(
    CloudServerConnection& server_connection) {
  AE_TELED_DEBUG("Set server {} to quarantine",
                 server_connection.server()->server_id);
  static constexpr Duration kQuarantineDuration =
      std::chrono::milliseconds{AE_CLOUD_SERVER_QUARANTINE_TIME_MS};
  // TODO: add task subscription here
  ae_context_.scheduler().DelayedTask(
      [this, sc{&server_connection}]() {
        AE_TELED_DEBUG("Release from quarantine server {}",
                       sc->server()->server_id);
        sc->quarantine(false);
        // update selected servers
        ReselectServers();
      },
      kQuarantineDuration);
  server_connection.quarantine(true);
}

std::vector<CloudServerConnection*> CloudServerConnections::ServerCandidates() {
  std::vector<CloudServerConnection*> servers;
  servers.reserve(server_connections_.size());
  for (auto& s : server_connections_) {
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

WriteAction& CloudServerConnections::CallApi(ApiCall const& api_caller,
                                             RequestPolicy::Variant policy) {
  std::vector<WriteAction*> swas;
  ForServers(
      [&](CloudServerConnection* sc) {
        auto* conn = sc->client_connection();
        assert((conn != nullptr) && "Client connection is null");
        swas.emplace_back(&conn->AuthorizedApiCall(
            SubApi<AuthorizedApi>{[&](auto& api) { api_caller(api, sc); }}));
      },
      policy);

  if (swas.empty()) {
    return EmptyWriteAction();
  }
  if (swas.size() == 1) {
    return *swas.front();
  }
  return ReplicaWriteAction(std::move(swas));
}

WriteAction& CloudServerConnections::EmptyWriteAction() {
  if (!empty_wa_ || empty_wa_->is_finished()) {
    empty_wa_.emplace(ae_context_);
  }
  return *empty_wa_;
}

WriteAction& CloudServerConnections::ReplicaWriteAction(
    std::vector<WriteAction*>&& swas) {
  // FIXME: this vector only grows
  return replica_was_.emplace_back(std::move(swas));
}

}  // namespace ae
