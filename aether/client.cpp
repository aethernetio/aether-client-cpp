/*
 * Copyright 2024 Aethernet Inc.
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

#include "aether/client.h"

#include <utility>

#include "aether/ae_actions/query_peer_receive_schedule.h"
#include "aether/ae_actions/telemetry.h"

#include "aether/aether.h"

namespace ae {

Client::Client() = default;

#ifdef AE_DISTILLATION
Client::Client(ObjProp prop, Aether::ptr aether)
    : Base{prop}, aether_{std::move(aether)} {}
#endif  // AE_DISTILLATION

Client::~Client() = default;

std::string const& Client::id() const { return client_id_; }
Uid const& Client::parent_uid() const { return parent_uid_; }
Uid const& Client::uid() const { return uid_; }
Uid const& Client::ephemeral_uid() const { return ephemeral_uid_; }
ServerKeys* Client::server_state(ServerId server_id) {
  auto ss_it = server_keys_.find(server_id);
  if (ss_it == server_keys_.end()) {
    auto [it, _] =
        server_keys_.emplace(server_id, ServerKeys{server_id, master_key_});
    ss_it = it;
  }
  return &ss_it->second;
}

Cloud::ptr const& Client::cloud() const { return cloud_; }

ClientCloudManager::ptr const& Client::cloud_manager() {
  // Lazy: ClientCloudManager::Init listens for cloud updates and therefore
  // calls cloud_connection(), which would start pings before SetReceiveSchedule
  // can run (SetReceiveSchedule must run after SetConfig, before first ping).
  if (!client_cloud_manager_) {
    client_cloud_manager_ = ClientCloudManager::ptr::Create(
        CreateWith{domain}.with_flags(ObjFlags::kUnloadedByDefault),
        Aether::ptr{aether_}, Client::ptr::MakeFromThis(this));
  }
  return client_cloud_manager_;
}

ServerConnectionManager& Client::server_connection_manager() {
  if (!server_connection_manager_) {
    auto aether = Aether::ptr{aether_};
    server_connection_manager_ = std::make_unique<ServerConnectionManager>(
        *aether, MakePtrFromThis(this));
  }
  return *server_connection_manager_;
}

CloudServerConnections& Client::cloud_connection() {
  if (!cloud_connection_) {
    cloud_connection_ = std::make_unique<CloudServerConnections>(
        *aether_, cloud_.Load(),
        server_connection_manager().GetServerConnectionFactory(),
        AE_CLOUD_MAX_SERVER_CONNECTIONS);

#if AE_ENABLE_PING
    ping_cloud_servers_ = std::make_unique<PingCloudServers>(
        *aether_, *cloud_connection_, *connectivity_policy().Load());
#endif

#if TELEMETRY_ENABLED
    // also create telemetry
    telemetry_ = std::make_unique<Telemetry>(*aether_, *cloud_connection_);
#endif
    // Cloud-config push subscription needs a live connection; start it here
    // (not in ClientCloudManager construction) so SetReceiveSchedule can run
    // after SetConfig and before the first cloud_connection().
    client_cloud_manager_.WithLoaded(
        [&](auto const& ccm) { ccm->StartCloudUpdateListener(); });
  }

  return *cloud_connection_;
}

ClientConnectivityPolicy::ptr const& Client::connectivity_policy() {
  assert(connectivity_policy_.is_valid());
  return connectivity_policy_;
}

P2pMessageStreamManager& Client::message_stream_manager() {
  if (!message_stream_manager_) {
    message_stream_manager_ = std::make_unique<P2pMessageStreamManager>(
        *aether_, MakePtrFromThis(this));
  }
  return *message_stream_manager_;
}

void Client::SetConfig(std::string client_id, Uid parent_uid, Uid uid,
                       Uid ephemeral_uid, Key master_key, Cloud::ptr cloud) {
  client_id_ = std::move(client_id);
  parent_uid_ = parent_uid;
  uid_ = uid;
  ephemeral_uid_ = ephemeral_uid;
  master_key_ = std::move(master_key);
  cloud_ = std::move(cloud);

  for (auto const& server_entry : cloud_->servers()) {
    auto const server_id = server_entry.first;
    server_keys_.emplace(server_id, ServerKeys{server_id, master_key_});
  }

  connectivity_policy_ = ClientConnectivityPolicy::ptr::Create(
      CreateWith{domain}.with_flags(ObjFlags::kUnloadedByDefault));
  // client_cloud_manager_ is created lazily in cloud_manager() so that
  // SetReceiveSchedule can configure RX timings before pings start.
}


Result<std::monostate, int> Client::SetReceiveSchedule(ReceiveSchedule schedule) {
  if (cloud_connection_) {
    return Error{static_cast<int>(SetReceiveScheduleError::kPingAlreadyStarted)};
  }
#if AE_ENABLE_PING
  if (ping_cloud_servers_) {
    return Error{static_cast<int>(SetReceiveScheduleError::kPingAlreadyStarted)};
  }
#endif
  if (!connectivity_policy_.is_valid()) {
    return Error{static_cast<int>(SetReceiveScheduleError::kPingAlreadyStarted)};
  }
  // Keep the policy loaded: it is created with kUnloadedByDefault, and a bare
  // Load()/configure would be lost when the temporary Ptr releases.
  connectivity_policy_keep_alive_ = connectivity_policy_.Load();
  connectivity_policy_keep_alive_->ConfigureRxTimings().ForAllPriorities(
      RxTimingConf{.interval = schedule.ping_interval,
                   .rx_window = schedule.receive_window});
  // Persist so a later Load() in cloud_connection() cannot revive defaults.
  connectivity_policy_.Save();
  return Ok{std::monostate{}};
}

::ae::QueryPeerReceiveSchedule& Client::QueryPeerReceiveSchedule(Uid peer_uid) {
  query_peer_receive_schedule_ =
      std::make_unique<::ae::QueryPeerReceiveSchedule>(
          AeContext{*aether_.Load().as<Aether>()}, *this, peer_uid);
  return *query_peer_receive_schedule_;
}

void Client::SendTelemetry() {
#if TELEMETRY_ENABLED
  telemetry_->SendTelemetry();
#endif
}

}  // namespace ae
