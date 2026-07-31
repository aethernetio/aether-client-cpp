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
#include <cassert>
#include <chrono>
#include <cstdint>
#include <utility>

#include "aether/api_protocol/api_protocol.h"
#include "aether/server.h"
#include "aether/server_connections/server_connection.h"

#include "aether/cloud_connections/cloud_connections_tele.h"

namespace ae {

namespace {
auto SelectedServersLog(
    [[maybe_unused]] std::vector<CloudServerConnection*> const&
        selected_servers) {
#if DEBUG
  std::vector<ServerId> server_ids;
  server_ids.reserve(selected_servers.size());
  for (auto* server_connection : selected_servers) {
    server_ids.emplace_back(server_connection->server()->server_id);
  }
  return server_ids;
#else
  return "!not a debug!";
#endif
}
}  // namespace

static constexpr auto kCloudServerQuarantineTime =
    std::chrono::milliseconds{AE_CLOUD_SERVER_QUARANTINE_TIME_MS};

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
  ReconcileServers();
}

CloudServerConnections::ServersUpdate::Subscriber
CloudServerConnections::servers_update_event() {
  return servers_update_event_;
}
CloudServerConnections::ServerQuarantineEvent::Subscriber
CloudServerConnections::server_quarantined_event() {
  return server_quarantined_event_;
}
CloudServerConnections::ServerQuarantineEvent::Subscriber
CloudServerConnections::server_quarantine_release_event() {
  return server_quarantine_release_event_;
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
  for (auto* server : selected_servers_) {
    server->Restream();
  }
}

void CloudServerConnections::InitServerConnections() {
  auto cloud = cloud_.Lock();
  assert(cloud != nullptr);
  server_connections_.clear();
  server_connections_.reserve(cloud->servers().size());
  for (auto& server : cloud->servers()) {
    server_connections_.emplace_back(server.Load(), *connection_factory_);
  }
  all_servers_.clear();
  all_servers_.reserve(server_connections_.size());
  for (auto& server : server_connections_) {
    all_servers_.emplace_back(&server);
  }
}

void CloudServerConnections::SubscribeToServerState(
    CloudServerConnection& server_connection) {
  auto* conn = server_connection.client_connection();
  if (conn == nullptr) {
    return;
  }
  auto const key = reinterpret_cast<std::uintptr_t>(&server_connection);
  auto& subs = server_subs_[key] = {};
  subs.state_sub = conn->stream_update_event().Subscribe(
      [this, sc{&server_connection}, conn]() {
        if (conn->stream_info().link_state == LinkState::kLinkError) {
          QuarantineServer(*sc);
          return true;
        }
        return false;
      });
  subs.error_sub = conn->server_connection().server_error_event().Subscribe(
      [this, sc{&server_connection}]() { QuarantineServer(*sc); });
}
void CloudServerConnections::UnsubscribeFromServerState(
    CloudServerConnection& server_connection) {
  auto const key = reinterpret_cast<std::uintptr_t>(&server_connection);
  auto it = server_subs_.find(key);
  if (it == server_subs_.end()) {
    return;
  }
  it->second.state_sub.Reset();
  it->second.error_sub.Reset();
  if (!it->second.quarantine_sub) {
    server_subs_.erase(it);
  }
}

void CloudServerConnections::QuarantineServer(
    CloudServerConnection& server_connection) {
  auto const key = reinterpret_cast<std::uintptr_t>(&server_connection);
  if (server_connection.quarantine()) {
    return;
  }
  AE_TELED_DEBUG("Quarantine server server_id={} priority={}",
                 server_connection.server()->server_id,
                 server_connection.priority());
  UnsubscribeFromServerState(server_connection);
  if (std::erase(selected_servers_, &server_connection) != 0) {
    UpdateSelectedPriorities();
    servers_update_event_.Emit();
  }
  server_connection.SetPriority(server_connections_.size());
  server_connection.SetQuarantine(true);
  server_quarantined_event_.Emit(&server_connection);
  server_subs_[key].quarantine_sub = ae_context_.scheduler().DelayedTask(
      [this, sc{&server_connection}, key]() {
        ReleaseQuarantinedServer(*sc, key);
      },
      kCloudServerQuarantineTime);
  if (!server_subs_[key].quarantine_sub) {
    assert(false && "Failed to schedule quarantine release task");
  }
  ScheduleReconcileServers();
}

void CloudServerConnections::ReleaseQuarantinedServer(
    CloudServerConnection& server_connection, std::uintptr_t key) {
  if (!server_connection.quarantine()) {
    return;
  }
  AE_TELED_DEBUG("Release quarantined server server_id={} priority={}",
                 server_connection.server()->server_id,
                 server_connection.priority());
  server_quarantine_release_event_.Emit(&server_connection);
  server_connection.Disconnect();
  server_connection.SetQuarantine(false);
  auto it = server_subs_.find(key);
  if (it != server_subs_.end()) {
    it->second.quarantine_sub.Reset();
    if (!it->second.state_sub && !it->second.error_sub) {
      server_subs_.erase(it);
    }
  }
  ScheduleReconcileServers();
}

void CloudServerConnections::ScheduleReconcileServers() {
  if (defer_sub_) {
    return;
  }
  defer_sub_ = ae_context_.scheduler().Task([this]() {
    defer_sub_.Reset();
    ReconcileServers();
  });
}

void CloudServerConnections::ReconcileServers() {
  if (selected_servers_.size() >= max_connections_) {
    return;
  }
  // Vacancy-fill model: keep the current selected list stable and only append
  // new non-selected candidates to fill available slots.
  auto candidates = ReplacementCandidates();
  AE_TELED_DEBUG(
      "Reconcile servers vacancy={} candidate_count={} selected_count={}",
      max_connections_ - selected_servers_.size(), candidates.size(),
      selected_servers_.size());
  auto emplaced = false;
  for (auto* candidate : candidates) {
    if (selected_servers_.size() >= max_connections_) {
      break;
    }
    candidate->SetPriority(selected_servers_.size());
    candidate->Connect();
    auto* conn = candidate->client_connection();
    if (conn == nullptr ||
        conn->stream_info().link_state == LinkState::kLinkError) {
      AE_TELED_DEBUG(
          "Candidate unusable during reconcile server_id={} priority={}",
          candidate->server()->server_id, candidate->priority());
      QuarantineServer(*candidate);
      continue;
    }
    selected_servers_.emplace_back(candidate);
    SubscribeToServerState(*candidate);
    emplaced = true;
  }
  if (emplaced) {
    AE_TELED_DEBUG("Selected servers reconciled selected_servers={}",
                   SelectedServersLog(selected_servers_));
    servers_update_event_.Emit();
  }
}

bool CloudServerConnections::IsSelected(CloudServerConnection* sc) const {
  return std::find(selected_servers_.begin(), selected_servers_.end(), sc) !=
         selected_servers_.end();
}

void CloudServerConnections::UpdateSelectedPriorities() {
  for (std::size_t i = 0; i < selected_servers_.size(); ++i) {
    selected_servers_.at(i)->SetPriority(i);
  }
}

std::vector<CloudServerConnection*>
CloudServerConnections::ReplacementCandidates() {
  std::vector<CloudServerConnection*> servers;
  servers.reserve(server_connections_.size());
  for (auto& s : server_connections_) {
    if (!s.quarantine() && !IsSelected(&s)) {
      servers.emplace_back(&s);
    }
  }
  std::sort(servers.begin(), servers.end(),
            [](auto const* left, auto const* right) {
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
        assert(conn != nullptr);
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
  return replica_was_.emplace_back(std::move(swas));
}

}  // namespace ae
