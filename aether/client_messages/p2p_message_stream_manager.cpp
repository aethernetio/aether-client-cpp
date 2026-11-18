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

#include "aether/client_messages/p2p_message_stream_manager.h"

#include <cassert>
#include <stdexcept>
#include <utility>

#include "aether/client.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/work_cloud_api/client_api/client_api_safe.h"

#include "aether/client_messages/client_messages_tele.h"
#include "aether/peer/peer_serialization.h"
#include "aether/peer/peer_transport.h"
#include "aether/peer/rendezvous_client.h"

namespace ae {

P2pMessageStreamManager::P2pMessageStreamManager(AeContext const& ae_context,
                                                 Ptr<Client> const& client)
    : ae_context_{ae_context},
      client_{client},
      cloud_connection_{&client->cloud_connection()},
      new_port_event_{ae_context_},
      on_message_received_sub_{CloudEventListener{
          ApiEventSubscriber{[this](ClientApiSafe& client_api,
                                    [[maybe_unused]] CloudServerConnection*
                                        server_connection) -> RegEventHandler {
            return client_api.send_message_event().Subscribe(
                [this](AeMessage const& message) {
                  NewMessageReceived(message);
                });
          }},
          *cloud_connection_, RequestPolicy::All{}}},
      route_changed_event_{ae_context_} {
  delivery_epoch_ = NewPeerDeliveryEpoch();
  AttachPeer();
}

P2pPortHandle P2pMessageStreamManager::CreatePort(Uid const& destination,
                                                  std::uint32_t stream,
                                                  PeerService service) {
  auto [port, is_new] = GetOrCreatePort(destination, stream, service);
  if (port->service() != service) {
    throw std::invalid_argument("Stream ID is bound to another service");
  }
  return P2pPortHandle{std::move(port)};
}

P2pMessageStreamManager::NewPortEvent const&
P2pMessageStreamManager::new_port_event() {
  return new_port_event_;
}

std::pair<std::shared_ptr<p2p_stream_internal::P2pReceivePort>, bool>
P2pMessageStreamManager::GetOrCreatePort(Uid const& destination,
                                         std::uint32_t stream,
                                         PeerService service) {
  CleanUpPorts();
  auto key = std::pair{destination, stream};
  auto it = ports_.find(key);
  if (it != std::end(ports_)) {
    if (auto existing = it->second.lock()) {
      return {std::move(existing), false};
    }
  }
  // Do not use std::make_shared here. GCC 16/libstdc++ emits a false
  // -Warray-bounds warning while destroying P2pReceivePort from
  // _Sp_counted_ptr_inplace because P2pReceivePort owns Event/RcPtr storage.
  auto port = std::shared_ptr<p2p_stream_internal::P2pReceivePort>{
      std::make_unique<p2p_stream_internal::P2pReceivePort>(
          ae_context_, destination, stream, service)};
  ports_[key] = port;
  return {port, true};
}

void P2pMessageStreamManager::NewMessageReceived(AeMessage const& message,
                                                 PeerRoute route) {
  AETHER_TELED_DEBUG("New message received {}", message.uid);
  PeerDelivery delivery;
  if (!PeerDecode(message.data, delivery) ||
      !receive_window_.Accept(message.uid, delivery)) {
    return;
  }
  auto [port, is_new] =
      GetOrCreatePort(message.uid, delivery.stream, delivery.service);
  if (port->service() != delivery.service) {
    return;
  }
  assert(port != nullptr);

  if (is_new) {
    auto handle = P2pPortHandle{port};
    new_port_event_.Emit(handle);
    if (handle) {
      return;
    }  // Application did not accept this incoming stream.
  }

  auto& stat =
      traffic_[{message.uid, delivery.stream}][static_cast<std::size_t>(route)];
  ++stat.received_messages;
  stat.received_bytes += delivery.data.size();
  port->Deliver(delivery.data);
}

DataBuffer P2pMessageStreamManager::Frame(std::uint32_t stream, DataBuffer data,
                                          PeerService service) {
  return PeerEncode(PeerDelivery{0x31444d41, delivery_epoch_,
                                 ++delivery_number_, stream, std::move(data),
                                 service});
}

void P2pMessageStreamManager::AttachPeer() {
  peer_received_sub_.Reset();
  peer_updates_sub_.Reset();
#if AE_SUPPORT_P2P
  auto client = client_.Lock();
  if (auto* peer = client->peer_transport()) {
    for (auto const& [uid, users] : route_users_) {
      peer->SetKeepBackupChannels(uid, users.first != 0);
    }
    peer_received_sub_ = peer->message_event().Subscribe(
        [this](Uid const& uid, DataBuffer const& data, PeerRoute route) {
          NewMessageReceived(AeMessage{uid, data}, route);
        });
    peer_updates_sub_ = peer->changed_event().Subscribe(
        [this] { route_changed_event_.Emit(); });
  }
#endif
  route_changed_event_.Emit();
}

void P2pMessageStreamManager::RecordSent(Uid const& destination,
                                         std::uint32_t stream, PeerRoute route,
                                         std::size_t bytes) {
  auto& stat = traffic_[{destination, stream}][static_cast<std::size_t>(route)];
  ++stat.sent_messages;
  stat.sent_bytes += bytes;
}

void P2pMessageStreamManager::RetainPeerRoute(Uid const& destination,
                                              bool keep_backup,
                                              bool hole_punching) {
  auto& users = route_users_[destination];
  ++(keep_backup ? users.first : users.second);
  if (hole_punching) {
    ++hole_users_[destination];
  }
#if AE_SUPPORT_P2P
  auto client = client_.Lock();
  if (auto* peer = client->peer_transport()) {
    peer->SetKeepBackupChannels(destination, users.first != 0);
  }
  if (hole_punching) {
#  if AE_SUPPORT_P2P && AE_SUPPORT_HOLE_PUNCH
    if (auto* rendezvous = client->rendezvous_client()) {
      rendezvous->RequestPeer(destination);
    }
#  endif
  }
#endif
}
void P2pMessageStreamManager::ReleasePeerRoute(Uid const& destination,
                                               bool keep_backup,
                                               bool hole_punching) {
  auto found = route_users_.find(destination);
  if (found == route_users_.end()) {
    return;
  }
  auto& users = found->second;
  auto& count = keep_backup ? users.first : users.second;
  assert(count != 0);
  --count;
  if (hole_punching) {
    auto hole = hole_users_.find(destination);
    if (hole != hole_users_.end() && --hole->second == 0) {
      hole_users_.erase(hole);
#if AE_SUPPORT_P2P
      if (auto client = client_.Lock()) {
#  if AE_SUPPORT_P2P && AE_SUPPORT_HOLE_PUNCH
        if (auto* rendezvous = client->rendezvous_client()) {
          rendezvous->ReleasePeer(destination);
        }
#  endif
      }
#endif
    }
  }
#if AE_SUPPORT_P2P
  auto client = client_.Lock();
  if (auto* peer = client->peer_transport()) {
    peer->SetKeepBackupChannels(destination,
                                users.first != 0 || users.second == 0);
  }
#endif
  if (users.first == 0 && users.second == 0) {
    route_users_.erase(found);
  }
}
void P2pMessageStreamManager::AttachRendezvous() {
#if AE_SUPPORT_P2P
  if (auto client = client_.Lock()) {
#  if AE_SUPPORT_P2P && AE_SUPPORT_HOLE_PUNCH
    if (auto* rendezvous = client->rendezvous_client()) {
      for (auto const& [uid, count] : hole_users_) {
        rendezvous->RequestPeer(uid);
      }
    }
#  endif
  }
#endif
}

std::array<TransportStatistics, kPeerRouteCount>
P2pMessageStreamManager::Statistics(Uid const& destination,
                                    std::uint32_t stream) const {
  auto found = traffic_.find({destination, stream});
  return found == traffic_.end()
             ? std::array<TransportStatistics, kPeerRouteCount>{}
             : found->second;
}

void P2pMessageStreamManager::CleanUpPorts() {
  std::erase_if(ports_, [](auto const& p) { return p.second.expired(); });
  std::erase_if(traffic_,
                [this](auto const& p) { return !ports_.contains(p.first); });
}

}  // namespace ae
