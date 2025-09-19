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

#include "aether/server_connections/client_server_connection.h"

#include "aether/server.h"
#include "aether/server_connections/server_channel.h"

#include "aether/tele/tele.h"

namespace ae {
ClientServerConnection::ChannelSelectStream ::ChannelSelectStream(
    ClientServerConnection& client_server_connection)
    : client_server_connection_{&client_server_connection},
      server_channel_{},
      stream_info_{} {
  SelectChannel();
}

ActionPtr<StreamWriteAction> ClientServerConnection::ChannelSelectStream::Write(
    DataBuffer&& data) {
  if (server_channel_ == nullptr) {
    SelectChannel();
  }
  if (server_channel_ == nullptr) {
    AE_TELED_ERROR("There is no channels, write failed");
    return ActionPtr<FailedStreamWriteAction>{
        client_server_connection_->action_context_};
  }
  AE_TELED_ERROR("Write to channel size {}", data.size());
  return server_channel_->stream().Write(std::move(data));
}

StreamInfo ClientServerConnection::ChannelSelectStream::stream_info() const {
  return stream_info_;
}

ClientServerConnection::ChannelSelectStream::StreamUpdateEvent::Subscriber
ClientServerConnection::ChannelSelectStream::stream_update_event() {
  return stream_update_event_;
}

ClientServerConnection::ChannelSelectStream::OutDataEvent::Subscriber
ClientServerConnection::ChannelSelectStream::out_data_event() {
  return out_data_event_;
}

ServerChannel const*
ClientServerConnection::ChannelSelectStream::server_channel() const {
  return server_channel_;
}

void ClientServerConnection::ChannelSelectStream::SelectChannel() {
  auto& channel_manager = client_server_connection_->channel_manager_;
  auto& channels = channel_manager.channels();
  // TODO: add select channel logic
  auto& channel_connection = channels.front();

  // TODO: how to reset channel?
  server_channel_ = &channel_connection.GetServerChannel();

  stream_update_sub_ =
      server_channel_->stream().stream_update_event().Subscribe(
          *this,
          MethodPtr<
              &ClientServerConnection::ChannelSelectStream::StreamUpdate>{});
  out_data_sub_ = server_channel_->stream().out_data_event().Subscribe(
      out_data_event_, MethodPtr<&OutDataEvent::Emit>{});

  // TODO: add handle for channels update
  StreamUpdate();
}

void ClientServerConnection::ChannelSelectStream::StreamUpdate() {
  stream_info_ = server_channel_->stream().stream_info();
  stream_update_event_.Emit();
}

ClientServerConnection::ClientServerConnection(ActionContext action_context,
                                               ObjPtr<Aether> const& aether,
                                               ObjPtr<Client> const& client,
                                               Server::ptr const& server)
    : action_context_{action_context},
      client_{client},
      server_{server},
      channel_manager_{action_context_, aether, server},
      client_to_server_stream_{action_context, client, server->server_id},
      channel_select_stream_{*this} {
  AE_TELED_DEBUG("Client server connection");
  Tie(client_to_server_stream_, channel_select_stream_);
  StreamUpdate();
  SubscribeToSelectChannel();
}

ClientToServerStream& ClientServerConnection::server_stream() {
  return client_to_server_stream_;
}

void ClientServerConnection::SubscribeToSelectChannel() {
  stream_update_sub_ = channel_select_stream_.stream_update_event().Subscribe(
      *this, MethodPtr<&ClientServerConnection::StreamUpdate>{});
}

void ClientServerConnection::StreamUpdate() {
  auto const* channel = channel_select_stream_.server_channel();
  // check if channel is updated
  if (server_channel_ == channel) {
    return;
  }
  server_channel_ = channel;

  Server::ptr server = server_.Lock();
  assert(server);
  // Create new ping if channel is updated
  // TODO: add ping interval config
  ping_ =
      OwnActionPtr<Ping>{action_context_, server, server_channel_->channel(),
                         client_to_server_stream_, std::chrono::seconds{5}};
}

}  // namespace ae
