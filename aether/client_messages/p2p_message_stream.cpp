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

#include "aether/client_messages/p2p_message_stream.h"

#include <algorithm>
#include <chrono>
#include "aether/channels/channel.h"

#include "aether/config.h"

#include "aether/client.h"
#include "aether/cloud.h"

#include "aether/cloud_connections/cloud_request.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/cloud_connections/cloud_subscription.h"

#include "aether/client_messages/client_messages_tele.h"
#include "aether/peer/peer_serialization.h"
#include "aether/peer/peer_transport.h"

namespace ae {
namespace p2p_stream_internal {
class MessageSendStream final : public IStream<AeMessage, AeMessage> {
 public:
  explicit MessageSendStream(EventContext auto const& context,
                             CloudServerConnections& cloud_connection,
                             RequestPolicy::Variant request_policy)
      : cloud_connection_{&cloud_connection},
        request_policy_{request_policy},
        out_data_event_{context},
        stream_update_event_{context},
        servers_update_sub_{cloud_connection_->servers_update_event().Subscribe(
            MethodPtr<&MessageSendStream::UpdateServers>{this})} {
    UpdateServers();
  }

  WriteAction& Write(AeMessage&& message) override {
    return cloud_connection_->CallApi(
        ApiCall{[&](ApiContext<AuthorizedApi>& auth_api, auto*) {
          auth_api->SendMessage(message);
        }},
        request_policy_);
  }

  StreamInfo stream_info() const override { return stream_info_; }
  OutDataEvent const& out_data_event() override { return out_data_event_; }
  StreamUpdateEvent const& stream_update_event() override {
    return stream_update_event_;
  }

  void Restream() override { cloud_connection_->Restream(); }

 private:
  void UpdateServers() {
    cloud_connection_->ForServers(
        [this](auto* sc) {
          if (auto* con = sc->client_connection(); con != nullptr) {
            streams_update_sub_ += con->stream_update_event().Subscribe(
                MethodPtr<&MessageSendStream::UpdateStream>{this});
          }
        },
        request_policy_);
    UpdateStream();
  }

  void UpdateStream() {
    std::vector<StreamInfo> infos;
    cloud_connection_->ForServers(
        [&](auto* sc) {
          if (auto* con = sc->client_connection(); con != nullptr) {
            infos.emplace_back(con->stream_info());
          }
        },
        request_policy_);

    if (infos.empty()) {
      stream_info_ = {};
      stream_update_event_.Emit();
      return;
    }

    // get min rec element size and min max element size
    stream_info_.rec_element_size = std::invoke([&]() {
      auto min = std::min_element(
          std::begin(infos), std::end(infos), [](auto const& a, auto const& b) {
            return a.rec_element_size < b.rec_element_size;
          });
      return min->rec_element_size;
    });
    stream_info_.max_element_size = std::invoke([&]() {
      auto min = std::min_element(
          std::begin(infos), std::end(infos), [](auto const& a, auto const& b) {
            return a.max_element_size < b.max_element_size;
          });
      return min->max_element_size;
    });
    // if any linked or unlinked or all error
    stream_info_.link_state = std::invoke([&]() {
      if (std::any_of(std::begin(infos), std::end(infos), [](auto const& i) {
            return i.link_state == LinkState::kLinked;
          })) {
        return LinkState::kLinked;
      }
      if (std::any_of(std::begin(infos), std::end(infos), [](auto const& i) {
            return i.link_state == LinkState::kUnlinked;
          })) {
        return LinkState::kUnlinked;
      }
      return LinkState::kLinkError;
    });
    // if any is writable
    stream_info_.is_writable = std::invoke([&]() {
      return std::any_of(std::begin(infos), std::end(infos),
                         [](auto const& i) { return i.is_writable; });
    });
    // if all reliable
    stream_info_.is_reliable = std::invoke([&]() {
      return std::all_of(std::begin(infos), std::end(infos),
                         [](auto const& i) { return i.is_reliable; });
    });

    AETHER_TELED_DEBUG(
        "Message send stream link state {}, rec_write_size {}, max_write_size "
        "{}",
        stream_info_.link_state, stream_info_.rec_element_size,
        stream_info_.max_element_size);

    stream_update_event_.Emit();
  }

  CloudServerConnections* cloud_connection_;
  RequestPolicy::Variant request_policy_;

  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;

  Subscription servers_update_sub_;
  MultiSubscription streams_update_sub_;
};
}  // namespace p2p_stream_internal

P2pStream::P2pStream(AeContext const& ae_context, Ptr<Client> const& client,
                     Uid destination, P2pPortHandle handle,
                     RequestPolicy::Variant send_request_policy,
                     StreamRoutePolicy route_policy,
                     StreamSendPolicy send_policy, RawSendBinding binding)
    : ae_context_{ae_context},
      client_{client},
      destination_{std::move(destination)},
      handle_{std::move(handle)},
      send_request_policy_{std::move(send_request_policy)},
      route_policy_{route_policy},
      send_policy_{send_policy},
      binding_{std::move(binding)},
      buffer_write_{ae_context_, MethodPtr<&P2pStream::OnWrite>{this}},
      out_data_event_{ae_context_},
      stream_update_event_{ae_context_},
      peer_writes_{ae_context_} {
  AETHER_TELE_DEBUG(kP2pMessageStreamNew, "P2pStream created for {}",
                    destination_);
  assert(!destination_.empty());
  if (!send_policy_.max_packet_bytes ||
      send_policy_.max_packet_bytes > AE_SOCKET_SEND_BUFFER_BYTES - 256) {
    throw std::invalid_argument("Invalid stream packet budget");
  }
  if (binding_.resource &&
      (!handle_ || send_policy_.max_packet_bytes <= kRawHeaderBytes)) {
    throw std::invalid_argument(
        "Balanced Raw needs a return port and frame budget");
  }
  auto& queue = binding_.resource ? *binding_.resource : client->send_queue();
  send_writable_sub_ = queue.writable_event().Subscribe([this] {
    if (!buffer_write_.is_buffer_on()) {
      buffer_write_.buffer_off();
    }
    auto owner = client_.Lock();
    bool writable = owner && HasCredit(*owner);
    if (queue_writable_ != writable) {
      queue_writable_ = writable;
      stream_update_event_.Emit();
    }
  });
  client->message_stream_manager().RetainPeerRoute(
      destination_, route_policy_.keep_backup_channels,
      route_policy_.hole_punching);

  if (handle_) {
    ConnectReceive();
  }
  ConnectSend();
#if AE_SUPPORT_P2P
  {
    peer_changed_sub_ =
        client->message_stream_manager().route_changed_event().Subscribe(
            [this] {
              auto client = client_.Lock();
              auto* peer = client ? client->peer_transport() : nullptr;
              if (peer && peer->BestPath(destination_, route_policy_.lan,
                                         route_policy_.direct_ip,
                                         route_policy_.hole_punching)) {
                buffer_write_.buffer_off();
                if (!route_policy_.keep_backup_channels) {
                  get_client_cloud_sub_.Reset();
                  message_send_stream_.reset();
                  dest_cloud_conn_.reset();
                  connecting_cloud_ = false;
                }
              } else if (!message_send_stream_ && !connecting_cloud_) {
                ConnectSend();
              }
              stream_update_event_.Emit();
            });
    auto* peer = client->peer_transport();
    if (peer &&
        peer->BestPath(destination_, route_policy_.lan, route_policy_.direct_ip,
                       route_policy_.hole_punching)) {
      buffer_write_.buffer_off();
    }
  }
#endif
  if (binding_.resource) {
    try {
      raw_flow_ = binding_.resource->OpenRawFlow(this, send_policy_.priority,
                                                 binding_.weight);
    } catch (...) {
      client->message_stream_manager().ReleasePeerRoute(
          destination_, route_policy_.keep_backup_channels,
          route_policy_.hole_punching);
      throw;
    }
  }
}

P2pStream::~P2pStream() {
  send_writable_sub_.Reset();
  if (raw_flow_) {
    binding_.resource->CloseRawFlow(*raw_flow_, this);
  }
  if (auto client = client_.Lock()) {
    if (!raw_flow_) {
      client->send_queue().Cancel(this);
    }
    client->message_stream_manager().ReleasePeerRoute(
        destination_, route_policy_.keep_backup_channels,
        route_policy_.hole_punching);
  }
}

WriteAction& P2pStream::Write(DataBuffer&& data) {
  if (raw_flow_) {
    if (auto* action = TryWriteRaw(std::move(data))) {
      return *action;
    }
  }
  if (raw_flow_ || data.size() > send_policy_.max_packet_bytes) {
    if (!failed_write_ || failed_write_->is_finished()) {
      failed_write_.emplace(ae_context_);
    }
    return *failed_write_;
  }
  AETHER_TELED_DEBUG("Write message for uid {} size:{}", destination_,
                     data.size());
  AeMessage message_data{destination_, std::move(data)};
  return buffer_write_.Write(std::move(message_data));
}

bool P2pStream::HasCredit(Client& client) const {
  auto bytes = send_policy_.max_packet_bytes + 128u;
  if (binding_.resource) {
    return raw_flow_ && binding_.resource->CanQueueRaw(*raw_flow_, bytes);
  }
  return client.send_queue().CanEnqueue(send_policy_.priority, bytes);
}
WriteAction* P2pStream::TryWriteRaw(DataBuffer&& data) {
  if (!raw_flow_ ||
      data.size() > send_policy_.max_packet_bytes - kRawHeaderBytes) {
    return nullptr;
  }
  unsigned bytes = static_cast<unsigned>(data.size()) + kRawHeaderBytes + 128u;
  if (!binding_.resource->CanQueueRaw(*raw_flow_, bytes)) {
    return nullptr;
  }
  // Bound retained allocation even for a small vector with a huge capacity.
  DataBuffer payload(data.begin(), data.end());
  auto* entry = binding_.resource->EnqueueRaw(
      *raw_flow_, this, bytes, [this, payload = std::move(payload)] {
        auto tag = binding_.resource->NextRawReceipt();
        AeMessage message{destination_, RawFrame(tag, payload)};
        std::optional<AeMessage> prepared;
        return SendNow(message, prepared);
      });
  auto writable = binding_.resource->CanQueueRaw(
      *raw_flow_, send_policy_.max_packet_bytes + 128u);
  if (writable != queue_writable_) {
    queue_writable_ = writable;
    stream_update_event_.Emit();
  }
  return entry;
}
P2pStream::StreamUpdateEvent const& P2pStream::stream_update_event() {
  return stream_update_event_;
}

StreamInfo P2pStream::stream_info() const {
#if AE_SUPPORT_P2P
  auto payload_limit = send_policy_.max_packet_bytes -
                       (binding_.resource ? kRawHeaderBytes : 0u);
  auto client = client_.Lock();
  if (client) {
    if (auto* peer = client->peer_transport();
        peer &&
        peer->BestPath(destination_, route_policy_.lan, route_policy_.direct_ip,
                       route_policy_.hole_punching)) {
      return {std::min<std::uint32_t>(1000, payload_limit), payload_limit,
              false, LinkState::kLinked, HasCredit(*client)};
    }
  }
#endif
  if (message_send_stream_) {
    auto info = message_send_stream_->stream_info();
    info.max_element_size = std::min<std::size_t>(
        info.max_element_size, send_policy_.max_packet_bytes);
    if (binding_.resource) {
      info.max_element_size -=
          std::min<std::size_t>(info.max_element_size, kRawHeaderBytes);
    }
    info.rec_element_size =
        std::min<std::size_t>(info.rec_element_size, info.max_element_size);
    auto client = client_.Lock();
    info.is_writable = info.is_writable && client && HasCredit(*client);
    return info;
  }
  return {};
}

P2pStream::OutDataEvent const& P2pStream::out_data_event() {
  return out_data_event_;
}

void P2pStream::Restream() {
  AETHER_TELED_DEBUG("Restream message stream");
  auto client_ptr = client_.Lock();
  assert(client_ptr);
  client_ptr->cloud_connection().Restream();
  if (message_send_stream_) {
    message_send_stream_->Restream();
  }
}

void P2pStream::WriteOut(DataBuffer const& data) {
  AETHER_TELED_DEBUG("Read message from uid {} {}", destination_, data.size());
  if (raw_flow_) {
    if (auto tag = ReadRawHeader(data, true)) {
      binding_.resource->ObserveRawReceipt(*raw_flow_, *tag);
    }
    return;
  }
  out_data_event_.Emit(data);
}

Uid const& P2pStream::destination() const { return destination_; }

void P2pStream::ConnectReceive() {
  out_data_sub_ =
      handle_.out_data_event().Subscribe(MethodPtr<&P2pStream::WriteOut>{this});
}

void P2pStream::ConnectSend() {
  if (!route_policy_.cloud || connecting_cloud_) {
    return;
  }
#if AE_SUPPORT_P2P
  auto owner = client_.Lock();
  auto* peer = owner ? owner->peer_transport() : nullptr;
  if (!route_policy_.keep_backup_channels && peer &&
      peer->BestPath(destination_, route_policy_.lan, route_policy_.direct_ip,
                     route_policy_.hole_punching)) {
    buffer_write_.buffer_off();
    return;
  }
#endif
  connecting_cloud_ = true;
  auto client_ptr = client_.Lock();
  assert(client_ptr);

  auto* get_client_cloud =
      client_ptr->cloud_manager()->TryGetCloud(destination_);
  if (get_client_cloud == nullptr) {
    connecting_cloud_ = false;
    ae_context_.scheduler().DelayedTask(
        cloud_retry_sub_, [this] { ConnectSend(); },
        std::chrono::milliseconds{100});
    return;
  }
  cloud_retry_sub_.Reset();

  get_client_cloud_sub_ = get_client_cloud->result_event().Subscribe(
      [this, client_ptr](Result<Cloud::ptr, int>&& result) {
        connecting_cloud_ = false;
        if (result) {
          auto cloud = std::move(result).value();
          dest_cloud_conn_ = MakeDestinationCloudConn(
              cloud.Load(), client_ptr->server_connection_manager()
                                .GetServerConnectionFactory());
          message_send_stream_ =
              std::make_unique<p2p_stream_internal::MessageSendStream>(
                  ae_context_, *dest_cloud_conn_, send_request_policy_);
          message_send_stream_->stream_update_event().Subscribe(
              MethodPtr<&StreamUpdateEvent::Emit>{&stream_update_event_});
          AETHER_TELED_DEBUG("Send connected");
          buffer_write_.buffer_off();
          stream_update_event_.Emit();
        } else {
          AETHER_TELED_ERROR("Send connection failed ");
#if AE_SUPPORT_P2P
          auto* peer = client_ptr->peer_transport();
          if (peer && peer->BestPath(destination_, route_policy_.lan,
                                     route_policy_.direct_ip,
                                     route_policy_.hole_punching)) {
            buffer_write_.buffer_off();
            return;
          }
#endif
          buffer_write_.Drop();
          buffer_write_.buffer_on();
        }
      });
}

std::unique_ptr<CloudServerConnections> P2pStream::MakeDestinationCloudConn(
    Ptr<Cloud> const& cloud,
    std::unique_ptr<IServerConnectionFactory> factory) {
  return std::make_unique<CloudServerConnections>(
      ae_context_, cloud, std::move(factory), AE_CLOUD_MAX_SERVER_CONNECTIONS);
}

WriteAction* P2pStream::OnWrite(AeMessage&& message) {
  auto client = client_.Lock();
  if (!client) {
    return nullptr;
  }
  auto& queue = client->send_queue();
  WriteAction* action{};
  if (queue.CanEnqueue(send_policy_.priority, message.data.size() + 128)) {
    auto bytes = message.data.size() + 128;
    action = queue.Enqueue(this, send_policy_.priority, bytes,
                           [this, message = std::move(message),
                            prepared = std::optional<AeMessage>{}]() mutable {
                             return SendNow(message, prepared);
                           });
  }
  bool writable = queue.CanEnqueue(send_policy_.priority,
                                   send_policy_.max_packet_bytes + 128);
  if (queue_writable_ != writable) {
    queue_writable_ = writable;
    stream_update_event_.Emit();
  }
  return action;
}

WriteAction* P2pStream::SendNow(AeMessage const& message,
                                std::optional<AeMessage>& prepared) {
  auto client = client_.Lock();
  if (!client) {
    return nullptr;
  }
  // A previous blocked attempt handed off no bytes. Allocate a fresh delivery
  // number only after selecting a writable route, preserving the dedup window.
  prepared.reset();
  auto frame = [&]() -> AeMessage& {
    if (!prepared) {
      prepared.emplace(AeMessage{
          destination_, client->message_stream_manager().Frame(
                            handle_ ? handle_.stream_id() : 0, message.data,
                            handle_ ? handle_.service() : PeerService{})});
    }
    return *prepared;
  };
  auto record_sent = [&](PeerRoute route) {
    client->message_stream_manager().RecordSent(
        destination_, handle_ ? handle_.stream_id() : 0, route,
        message.data.size());
  };
#if AE_SUPPORT_P2P
  if (auto* peer = client->peer_transport()) {
    if (auto path = peer->BestPath(destination_, route_policy_.lan,
                                   route_policy_.direct_ip,
                                   route_policy_.hole_punching);
        path && message.data.size() < 57000) {
      auto* action = peer_writes_.Create(ae_context_, true);
      if (!action) {
        return nullptr;
      }
      auto selected_route = path->route;
      if (peer->Send(destination_, frame().data, route_policy_.lan,
                     route_policy_.direct_ip, &selected_route,
                     route_policy_.hole_punching)) {
        record_sent(selected_route);
        return action;
      }
      if (auto* current = client->peer_transport();
          current && current->BestPath(destination_, route_policy_.lan,
                                       route_policy_.direct_ip,
                                       route_policy_.hole_punching)) {
        return nullptr;  // Socket backpressure retains the direct route.
      }
    }
  }
#endif
  if (!message_send_stream_) {
    return nullptr;
  }
  auto info = message_send_stream_->stream_info();
  if (info.link_state != LinkState::kLinked || !info.is_writable) {
    return nullptr;
  }
  record_sent(PeerRoute::kCloud);
  return &message_send_stream_->Write(std::move(frame()));
}

std::array<TransportStatistics, kPeerRouteCount> P2pStream::statistics() const {
  auto owner = client_.Lock();
  auto result = owner->message_stream_manager().Statistics(
      destination_, handle_ ? handle_.stream_id() : 0);
  if (dest_cloud_conn_) {
    dest_cloud_conn_->ForServers(
        [&](auto* server) {
          auto* connection = server->client_connection();
          if (!connection) {
            return;
          }
          auto channel = connection->server_connection().current_channel();
          if (!channel) {
            return;
          }
          auto const& samples =
              channel->channel_statistics().response_time_statistics();
          if (!samples.empty()) {
            auto sample = samples.template percentile<50>().count();
            if (!result[0].rtt_us || sample < *result[0].rtt_us) {
              result[0].rtt_us = sample;
            }
          }
        },
        send_request_policy_);
  }
#if AE_SUPPORT_P2P
  auto client = client_.Lock();
  if (client) {
    if (auto* peer = client->peer_transport()) {
      for (auto const& info : peer->registry().clients()) {
        if (info.uid != destination_) {
          continue;
        }
        for (auto const& path : info.paths) {
          auto& stat = result[static_cast<std::size_t>(path.route)];
          stat.wire_sent_bytes += path.statistics.wire_sent_bytes;
          stat.wire_received_bytes += path.statistics.wire_received_bytes;
          stat.probes_sent += path.statistics.probes_sent;
          stat.probes_received += path.statistics.probes_received;
          stat.probes_lost += path.statistics.probes_lost;
          if (path.connected) {
            stat.recent_loss_per_mille = path.statistics.recent_loss_per_mille;
            stat.loss_samples = path.statistics.loss_samples;
          }
          if (path.connected && path.statistics.rtt_us &&
              (!stat.rtt_us || *path.statistics.rtt_us < *stat.rtt_us)) {
            stat.rtt_us = path.statistics.rtt_us;
            stat.jitter_us = path.statistics.jitter_us;
          }
        }
      }
    }
  }
#endif
  return result;
}

P2pStream::SendPathTransportView P2pStream::InspectSendPathTransport() const {
  SendPathTransportView view;
  view.aggregated = stream_info();
#if AE_SUPPORT_P2P
  auto client = client_.Lock();
  if (client) {
    if (auto* peer = client->peer_transport();
        peer &&
        peer->BestPath(destination_, route_policy_.lan, route_policy_.direct_ip,
                       route_policy_.hole_punching)) {
      view.linked_udp_endpoint = true;
      return view;
    }
  }
#endif
  if (dest_cloud_conn_) {
    dest_cloud_conn_->ForServers(
        [&](auto* sc) {
          auto* con = sc->client_connection();
          if (con == nullptr ||
              con->stream_info().link_state != LinkState::kLinked) {
            return;
          }
          auto channel = con->server_connection().current_channel();
          if (channel == nullptr) {
            return;
          }
          auto endpoint = channel->endpoint();
          if (!endpoint) {
            return;
          }
          view.linked_udp_endpoint |= endpoint->protocol == Protocol::kUdp;
          view.linked_tcp_endpoint |= endpoint->protocol == Protocol::kTcp;
        },
        send_request_policy_);
  }
  return view;
}
std::optional<std::span<std::uint8_t const>> RawReceiptSink::Receive(
    P2pStream& reverse, DataBuffer const& frame) {
  auto tag = ReadRawHeader(frame, false);
  if (!tag) {
    return {};
  }
  if (!sending_ && reverse.stream_info().is_writable) {
    auto& write = reverse.Write(RawAck(*tag));
    sending_ =
        write.status_event().Subscribe([this](auto) { sending_.Reset(); });
  }
  return std::span{frame}.subspan(kRawHeaderBytes);
}
}  // namespace ae
