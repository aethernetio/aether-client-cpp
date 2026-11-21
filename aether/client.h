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

#ifndef AETHER_CLIENT_H_
#define AETHER_CLIENT_H_

#include <cassert>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "aether-objects/obj/obj.h"
#include "aether/client_connectivity_policy.h"
#include "aether/cloud.h"
#include "aether/memory.h"
#include "aether/remote_presence.h"
#include "aether/server_keys.h"
#include "aether/types/uid.h"

#include "aether/cloud_connections/cloud_server_connections.h"
#include "aether/cloud_connections/ping_cloud_servers.h"

#include "aether/connection_manager/client_cloud_manager.h"
#include "aether/connection_manager/server_connection_manager.h"

#include "aether/client_messages/p2p_message_stream_manager.h"
#include "aether/peer/peer_types.h"
#include "aether/streams/stream_definition.h"
#include "aether/streams/stream_definition_v0.h"

namespace ae {
class Aether;
class Telemetry;
class PeerPresenceQuery;
class PeerTransport;
class CloudRequest;
class StreamHierarchy;
class RendezvousClient;
class P2pStream;
class PrioritySendQueue;

class Client : public Obj {
  AE_OBJECT(Client, Obj, 1)

  Client();

 public:
  // Internal
#ifdef AE_DISTILLATION
  Client(ObjProp prop, ObjPtr<Aether> aether);
#endif  // AE_DISTILLATION
  ~Client() override;

  // Public API.
  std::string const& id() const;
  Uid const& parent_uid() const;
  Uid const& uid() const;
  Uid const& ephemeral_uid() const;
  ServerKeys* server_state(ServerId server_id);
  Cloud::ptr const& cloud() const;
  ClientCloudManager::ptr const& cloud_manager() const;
  ServerConnectionManager& server_connection_manager();
  CloudServerConnections& cloud_connection();
  ClientConnectivityPolicy::ptr const& connectivity_policy();
  P2pMessageStreamManager& message_stream_manager();
  PrioritySendQueue& send_queue();

  bool IsLocallyOnline() const noexcept;
  bool IsAetherOnline() const noexcept;
  ClientPresence GetPresence() const;
  PeerPresence GetPeerPresence(Uid peer_uid) const;
  ae::PeerPresenceQuery& QueryPeerPresence(Uid peer_uid);
  // Retains the serialized definitions in every build.
  void ConfigureStreams(std::vector<StreamDefinition> definitions);
#if AE_ENABLE_STREAM_HIERARCHY
  StreamHierarchy& streams();
#endif
#if AE_SUPPORT_P2P
  void ConfigurePeer(PeerSettings settings);
  PeerSettings const& peer_settings() const { return peer_settings_; }
  PeerIdentity const& peer_identity() const { return peer_identity_; }
  PeerTransport* peer_transport();
  void RenewPeerIdentity();
  RendezvousClient* rendezvous_client() const {
#  if AE_SUPPORT_HOLE_PUNCH
    return rendezvous_client_.get();
#  else
    return nullptr;
#  endif
  }
#endif

  void SetConfig(std::string client_id, Uid parent_uid, Uid uid,
                 Uid ephemeral_uid, Key master_key, Cloud::ptr c,
                 PeerIdentity peer_identity = {});

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, client_id_, parent_uid_, uid_,
                             ephemeral_uid_, master_key_, cloud_, server_keys_,
                             connectivity_policy_, client_cloud_manager_))
  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_);
    dnv(aether_, client_id_, parent_uid_, uid_, ephemeral_uid_, master_key_,
        cloud_, server_keys_, connectivity_policy_, client_cloud_manager_);
    dnv(peer_identity_, peer_settings_, stream_definitions_);
  }
  template <typename Dnv>
  void Load(Version<0>, Dnv& dnv) {
    dnv(base_);
    dnv(aether_, client_id_, parent_uid_, uid_, ephemeral_uid_, master_key_,
        cloud_, server_keys_, connectivity_policy_, client_cloud_manager_);
    std::vector<stream_definition_internal::Version0> definitions;
    dnv(peer_identity_, peer_settings_, definitions);
    stream_definitions_.clear();
    for (auto& definition : definitions) {
      stream_definitions_.push_back(std::move(definition).Upgrade());
    }
  }
  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_);
    dnv(aether_, client_id_, parent_uid_, uid_, ephemeral_uid_, master_key_,
        cloud_, server_keys_, connectivity_policy_, client_cloud_manager_);
    dnv(peer_identity_, peer_settings_, stream_definitions_);
  }
  void SendTelemetry();

 private:
  friend class PeerPresenceQuery;
  OnlineType DirectOnlineTypes(Uid const* peer_uid = nullptr) const;
  PeerPresenceState DirectPresenceState(Uid peer_uid) const;
  void Loaded();
#if AE_SUPPORT_P2P
  void EnsurePeerIdentity();
  void StartPeerRendezvous();
#endif
  ObjPtr<Aether> aether_;
  // configuration
  std::string client_id_;  // User-defined client id
  Uid parent_uid_;         // Parent aethernet client uid
  Uid uid_{};              // Aethernet client uid
  Uid ephemeral_uid_{};    // Used for server aethentication
  Key master_key_;
  PeerIdentity peer_identity_;
  PeerSettings peer_settings_;
  std::vector<StreamDefinition> stream_definitions_;
  Cloud::ptr cloud_;

  // states
  std::map<ServerId, ServerKeys> server_keys_;

  ClientConnectivityPolicy::ptr connectivity_policy_;
  ClientCloudManager::ptr client_cloud_manager_;
  std::unique_ptr<ServerConnectionManager> server_connection_manager_;
  std::unique_ptr<CloudServerConnections> cloud_connection_;
  std::unique_ptr<PrioritySendQueue> send_queue_;
  std::unique_ptr<P2pMessageStreamManager> message_stream_manager_;
  std::map<Uid, std::unique_ptr<ae::PeerPresenceQuery>> peer_presence_queries_;
  std::map<Uid, std::vector<RemoteServerPresenceSample>> peer_cloud_presence_;
#if AE_ENABLE_STREAM_HIERARCHY
  std::unique_ptr<StreamHierarchy> stream_hierarchy_;
#endif
#if AE_SUPPORT_P2P
  std::unique_ptr<PeerTransport> peer_transport_;
#  if AE_SUPPORT_HOLE_PUNCH
  std::unique_ptr<P2pStream> rendezvous_signaling_;
  std::unique_ptr<RendezvousClient> rendezvous_client_;
#  endif
  std::unique_ptr<CloudRequest> peer_identity_request_;
  TaskSubscription peer_start_task_;
  TaskSubscription peer_identity_task_;
#endif

#if AE_ENABLE_PING
  std::unique_ptr<PingCloudServers> ping_cloud_servers_;
#endif
#if AE_TELE_ENABLED && AE_TELE_LOG_TO_STATISTICS
  std::unique_ptr<Telemetry> telemetry_;
#endif
};
}  // namespace ae

#endif  // AETHER_CLIENT_H_
