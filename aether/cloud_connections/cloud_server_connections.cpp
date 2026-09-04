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
#include <cstddef>
#include <utility>

#include "aether/api_protocol/api_protocol.h"
#include "aether/server_connections/server_connection.h"

#include "aether/cloud_connections/cloud_connections_tele.h"  // IWYU pragma: keep

namespace ae {
#if DEBUG
auto ServerList(std::vector<CloudServerConnection*> const& servers) {
  std::vector<ServerId> sids;
  sids.reserve(servers.size());
  for (auto const* s : servers) {
    sids.emplace_back(s->server_id());
  }
  return sids;
}
#else
std::string_view ServerList(std::vector<CloudServerConnection*> const&) {
  return "!no debug!";
}
#endif

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
  if (max_connections_ != 0) {
    ReconcileServers();
  }
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

void CloudServerConnections::QuarantineForNoResponse(
    CloudServerConnection& server_connection) {
  QuarantineAndReconcile(server_connection);
}

void CloudServerConnections::InitServerConnections() {
  auto cloud = cloud_.Lock();
  assert(cloud && "cloud must outlive its connections");
  assert(server_entries_.empty() &&
         "server entries must be initialized only once");
  server_entries_.reserve(cloud->servers().size());
  for (auto const& cloud_server : cloud->servers()) {
    server_entries_.emplace_back(cloud, cloud_server.first,
                                 *connection_factory_);
  }
  all_servers_.reserve(server_entries_.size());
  for (auto& entry : server_entries_) {
    all_servers_.emplace_back(&entry.connection);
  }
  std::sort(all_servers_.begin(), all_servers_.end(),
            [](auto const* left, auto const* right) {
              return left->priority() < right->priority();
            });
  NormalizeServerPriorities();
}

bool CloudServerConnections::SubscribeToServerState(
    CloudServerConnection& server_connection) {
  auto* conn = server_connection.client_connection();
  if (conn == nullptr ||
      conn->stream_info().link_state == LinkState::kLinkError) {
    return false;
  }

  if (conn->stream_info().link_state == LinkState::kLinked) {
    AE_TELED_DEBUG("CLOUD_SERVER_LINKED server_id={} priority={}",
                   server_connection.server_id(), server_connection.priority());
  }

  auto& entry = ServerEntryFor(server_connection);
  entry.state_sub = conn->stream_update_event().Subscribe(
      [this, sc{&server_connection}, conn]() {
        if (conn->stream_info().link_state == LinkState::kLinkError) {
          QuarantineAndReconcile(*sc);
        } else if (conn->stream_info().link_state == LinkState::kLinked) {
          AE_TELED_DEBUG("CLOUD_SERVER_LINKED server_id={} priority={}",
                         sc->server_id(), sc->priority());
        }
      });
  entry.error_sub = conn->server_connection().server_error_event().Subscribe(
      [this, sc{&server_connection}]() { QuarantineAndReconcile(*sc); });
  return true;
}
void CloudServerConnections::UnsubscribeFromServerState(
    CloudServerConnection& server_connection) {
  auto& entry = ServerEntryFor(server_connection);
  entry.state_sub.Reset();
  entry.error_sub.Reset();
}

bool CloudServerConnections::QuarantineServer(
    CloudServerConnection& server_connection) {
  if (server_connection.quarantine()) {
    return false;
  }
  AE_TELED_DEBUG("CLOUD_SERVER_QUARANTINED server_id={} priority={}",
                 server_connection.server_id(), server_connection.priority());
  UnsubscribeFromServerState(server_connection);
  auto const was_selected =
      std::erase(selected_servers_, &server_connection) != 0;
  assert(!server_entries_.empty() &&
         "quarantined server must belong to server connections");
  AE_TELED_DEBUG("CLOUD_SERVER_UNSELECTED server_id={}, selected list={}",
                 server_connection.server_id(), ServerList(selected_servers_));
  auto const server_it =
      std::find(all_servers_.begin(), all_servers_.end(), &server_connection);
  assert(server_it != all_servers_.end() &&
         "quarantined server must belong to all servers");
  std::rotate(server_it, server_it + 1, all_servers_.end());
  server_connection.SetQuarantine(true);
  NormalizeServerPriorities();
  if (was_selected) {
    servers_update_event_.Emit();
  }
  server_quarantined_event_.Emit(&server_connection);

  // One delayed release: Disconnect + clear quarantine + reconcile. Do not
  // Disconnect on the error-callback stack.
  auto& quarantine_sub = ServerEntryFor(server_connection).quarantine_sub;
  quarantine_sub = ae_context_.scheduler().DelayedTask(
      [this, sc{&server_connection}]() { ReleaseQuarantinedServer(*sc); },
      kCloudServerQuarantineTime);

  if (!quarantine_sub) {
    AE_TELED_ERROR("CLOUD_QUARANTINE_RELEASE_ALLOC_FAILED");
    assert(false && "failed to schedule quarantine release");
  }

  return true;
}

void CloudServerConnections::QuarantineAndReconcile(
    CloudServerConnection& server_connection) {
  if (QuarantineServer(server_connection)) {
    ScheduleReconcileServers();
  }
}

void CloudServerConnections::ReleaseQuarantinedServer(
    CloudServerConnection& server_connection) {
  if (!server_connection.quarantine()) {
    return;
  }
  AE_TELED_DEBUG("CLOUD_SERVER_RELEASED server_id={} priority={}",
                 server_connection.server_id(), server_connection.priority());
  server_connection.Disconnect();
  auto const first_quarantined =
      std::find_if(all_servers_.begin(), all_servers_.end(),
                   [](auto const* server) { return server->quarantine(); });
  auto const server_it =
      std::find(all_servers_.begin(), all_servers_.end(), &server_connection);
  assert(first_quarantined != all_servers_.end() &&
         "released server must be in quarantined suffix");
  assert(server_it != all_servers_.end() &&
         "released server must belong to all servers");
  std::rotate(first_quarantined, server_it, server_it + 1);
  server_connection.SetQuarantine(false);
  NormalizeServerPriorities();
  server_quarantine_release_event_.Emit(&server_connection);
  ServerEntryFor(server_connection).quarantine_sub.Reset();

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
  if (!defer_sub_) {
    AE_TELED_ERROR(
        "CLOUD_SCHEDULE_RECONCILE_ALLOC_FAILED; pending until release");
    assert(false && "failed to schedule reconcile servers");
  }
}

void CloudServerConnections::ReconcileServers() {
  if (selected_servers_.size() >= max_connections_) {
    return;
  }
  // Vacancy-fill model: keep the current selected list stable and only append
  // usable servers to fill available slots. The selected prefix is skipped;
  // failed candidates move to the quarantined suffix, so retry the same index.
  AE_TELED_DEBUG("Reconcile servers vacancy={} selected_count={}",
                 max_connections_ - selected_servers_.size(),
                 selected_servers_.size());
  auto emplaced = false;
  auto candidate_index = selected_servers_.size();
  while (candidate_index < all_servers_.size() &&
         selected_servers_.size() < max_connections_) {
    auto* candidate = all_servers_[candidate_index];
    if (candidate->quarantine()) {
      ++candidate_index;
      continue;
    }
    AE_TELED_DEBUG("CLOUD_SERVER_CONNECT_ATTEMPT server_id={} priority={}",
                   candidate->server_id(), candidate->priority());

    if (!candidate->Connect() || !SubscribeToServerState(*candidate)) {
      AE_TELED_DEBUG(
          "Candidate unusable during reconcile server_id={} priority={}",
          candidate->server_id(), candidate->priority());
      QuarantineAndReconcile(*candidate);
      continue;
    }
    assert(candidate_index == selected_servers_.size() &&
           "candidate must follow selected prefix");
    selected_servers_.emplace_back(candidate);
    candidate_index = selected_servers_.size();
    emplaced = true;
  }
  if (emplaced) {
    NormalizeServerPriorities();
    AE_TELED_DEBUG("Selected servers reconciled selected_count={}, list={}",
                   selected_servers_.size(), ServerList(selected_servers_));
    servers_update_event_.Emit();
  }
}

CloudServerConnections::ServerEntry& CloudServerConnections::ServerEntryFor(
    CloudServerConnection& server_connection) {
  auto const entry_it =
      std::find_if(server_entries_.begin(), server_entries_.end(),
                   [&server_connection](ServerEntry const& entry) {
                     return &entry.connection == &server_connection;
                   });
  assert(entry_it != server_entries_.end() &&
         "server connection must belong to server entries");
  return *entry_it;
}

void CloudServerConnections::NormalizeServerPriorities() {
  for (std::size_t i = 0; i < all_servers_.size(); ++i) {
    all_servers_[i]->SetPriority(i);
  }
}

WriteAction& CloudServerConnections::CallApi(ApiCall const& api_caller,
                                             RequestPolicy::Variant policy) {
  std::vector<WriteAction*> swas;
  ForServers(
      [&](CloudServerConnection* sc) {
        auto* conn = sc->client_connection();
        if (conn == nullptr) {
          AE_TELED_WARNING("Skipping disconnected selected server");
          return;
        }
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
