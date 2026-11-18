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

#include <stdexcept>
#include <utility>

#include "aether/ae_actions/telemetry.h"
#include "aether/connection_manager/peer_presence_query.h"

#include "aether/aether.h"
#include "aether/cloud_connections/cloud_request.h"
#include "aether/peer/peer_transport.h"
#include "aether/peer/rendezvous_client.h"
#include "aether/streams/stream_hierarchy.h"

namespace ae {

Client::Client() = default;

#ifdef AE_DISTILLATION
Client::Client(ObjProp prop, Aether::ptr aether)
    : Base{prop}, aether_{std::move(aether)} {}
#endif  // AE_DISTILLATION

Client::~Client() = default;

void Client::Loaded() {
  AeContext context{*this};
#if AE_SUPPORT_P2P
  if (!peer_settings_.app_id.empty() || !stream_definitions_.empty()) {
    context.scheduler().Task(peer_start_task_, [this] {
      peer_transport();
      message_stream_manager();
      streams();
    });
  }
#else
  if (!stream_definitions_.empty()) {
    streams();
  }
#endif
}

void Client::ConfigureStreams(std::vector<StreamDefinition> definitions) {
  auto hierarchy = std::make_unique<StreamHierarchy>(
      AeContext{*this}, MakePtrFromThis(this), definitions);
  stream_hierarchy_ = std::move(hierarchy);
  stream_definitions_ = std::move(definitions);
}
StreamHierarchy& Client::streams() {
  if (!stream_hierarchy_) {
    stream_hierarchy_ = std::make_unique<StreamHierarchy>(
        AeContext{*this}, MakePtrFromThis(this), stream_definitions_);
  }
  return *stream_hierarchy_;
}

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

ClientCloudManager::ptr const& Client::cloud_manager() const {
  assert(client_cloud_manager_.is_valid());
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

    client_cloud_manager_.WithLoaded(
        [&](auto const& ccm) { ccm->StartListenForCloudUpdate(); });
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
#if AE_SUPPORT_P2P
    StartPeerRendezvous();
#endif
  }
  return *message_stream_manager_;
}

bool Client::IsLocallyOnline() const noexcept {
  return GetPresence().IsOnline();
}
bool Client::IsAetherOnline() const noexcept {
  return connectivity_policy_ && connectivity_policy_.Load()->IsLocallyOnline();
}

OnlineType Client::DirectOnlineTypes(Uid const* uid) const {
#if AE_SUPPORT_P2P
  return peer_transport_ ? peer_transport_->OnlineTypes(uid)
                         : OnlineType::kNone;
#else
  static_cast<void>(uid);
  return OnlineType::kNone;
#endif
}
ClientPresence Client::GetPresence() const {
  auto aether = IsAetherOnline();
  return {aether, DirectOnlineTypes() |
                      (aether ? OnlineType::kAethernet : OnlineType::kNone)};
}
PeerPresenceState Client::DirectPresenceState(Uid uid) const {
#if AE_SUPPORT_P2P
  if (peer_transport_) {
    return peer_transport_->PresenceState(uid);
  }
#else
  static_cast<void>(uid);
#endif
  return PeerPresenceState::kUnknown;
}
PeerPresence Client::GetPeerPresence(Uid uid) const {
  PeerPresence result;
  if (auto cached = peer_cloud_presence_.find(uid);
      cached != peer_cloud_presence_.end()) {
    auto samples = cached->second;
    for (auto& sample : samples) {
      if (sample.status == RemoteServerPresence::kOnline &&
          Now() > sample.offline_deadline) {
        sample.status = RemoteServerPresence::kUnknown;
      }
    }
    result = AggregateRemotePresence(samples);
  }
  return CombinePeerPresence(result, DirectOnlineTypes(&uid), IsAetherOnline(),
                             DirectPresenceState(uid));
}

ae::PeerPresenceQuery& Client::QueryPeerPresence(Uid peer_uid) {
  auto& query = peer_presence_queries_[peer_uid];
  if (!query || query->is_finished()) {
    query = std::make_unique<ae::PeerPresenceQuery>(AeContext{*aether_}, *this,
                                                    peer_uid);
  }
  return *query;
}

void Client::SetConfig(std::string client_id, Uid parent_uid, Uid uid,
                       Uid ephemeral_uid, Key master_key, Cloud::ptr cloud,
                       PeerIdentity peer_identity) {
  if (peer_identity.certified && peer_identity.certificate.uid != uid) {
    throw std::invalid_argument("Peer identity belongs to another UID");
  }
  client_id_ = std::move(client_id);
  parent_uid_ = parent_uid;
  uid_ = uid;
  ephemeral_uid_ = ephemeral_uid;
  master_key_ = std::move(master_key);
  peer_identity_ = std::move(peer_identity);
  cloud_ = std::move(cloud);

  for (auto const& server_entry : cloud_->servers()) {
    auto const server_id = server_entry.first;
    server_keys_.emplace(server_id, ServerKeys{server_id, master_key_});
  }

  connectivity_policy_ = ClientConnectivityPolicy::ptr::Create(
      CreateWith{domain}.with_flags(ObjFlags::kUnloadedByDefault));

  client_cloud_manager_ = ClientCloudManager::ptr::Create(
      CreateWith{domain}.with_flags(ObjFlags::kUnloadedByDefault),
      Aether::ptr{aether_}, Client::ptr::MakeFromThis(this));
}

void Client::SendTelemetry() {
#if TELEMETRY_ENABLED
  telemetry_->SendTelemetry();
#endif
}

}  // namespace ae
