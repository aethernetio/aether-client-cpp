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

#include "aether/cloud.h"
#include "aether/client.h"

#include "aether/client_messages/client_messages_tele.h"

namespace ae {
P2pStream::P2pStream(ActionContext action_context, ObjPtr<Client> const& client,
                     Uid destination)
    : action_context_{action_context},
      client_{client},
      destination_{destination},
      // TODO: add buffer config
      buffer_stream_{action_context_, 100} {
  AE_TELE_DEBUG(kP2pMessageStreamNew, "P2pStream created for {}", destination_);
  // destination uid must not be empty
  assert(!destination_.empty());

  ConnectReceive();
  ConnectSend();
}

P2pStream::~P2pStream() = default;

ActionPtr<StreamWriteAction> P2pStream::Write(DataBuffer&& data) {
  AE_TELED_DEBUG("Write message {}", data.size());
  MessageData message_data{destination_, std::move(data)};
  return buffer_stream_.Write(std::move(message_data));
}

P2pStream::StreamUpdateEvent::Subscriber P2pStream::stream_update_event() {
  return buffer_stream_.stream_update_event();
}

StreamInfo P2pStream::stream_info() const {
  // TODO: Combine info for receive and send streams
  return buffer_stream_.stream_info();
}

P2pStream::OutDataEvent::Subscriber P2pStream::out_data_event() {
  return out_data_event_;
}

void P2pStream::Restream() {
  if (destination_stream_) {
    destination_stream_->Restream();
  }
  if (receive_stream_ != nullptr) {
    receive_stream_->Restream();
  }
}

void P2pStream::WriteOut(DataBuffer const& data) { out_data_event_.Emit(data); }

Uid P2pStream::destination() const { return destination_; }

void P2pStream::ConnectReceive() {
  auto client_ptr = client_.Lock();
  assert(client_ptr);
  receive_stream_ = &client_ptr->cloud_message_stream();
  out_data_sub_ = receive_stream_->out_data_event().Subscribe(
      *this, MethodPtr<&P2pStream::DataReceived>{});
}

void P2pStream::ConnectSend() {
  auto client_ptr = client_.Lock();
  assert(client_ptr);

  auto get_client_cloud = client_ptr->cloud_manager()->GetCloud(destination_);

  get_client_connection_subscription_ =
      get_client_cloud->StatusEvent().Subscribe(
          OnResult{[this](GetCloudAction& action) {
            auto cloud = action.cloud();
            destination_connection_manager_ = MakeConnectionManager(cloud);
            destination_stream_ =
                MakeDestinationStream(*destination_connection_manager_);
            AE_TELED_DEBUG("Send connected");
            Tie(buffer_stream_, *destination_stream_);
          }});
}

void P2pStream::DataReceived(MessageData const& data) {
  if (data.destination != destination_) {
    return;
  }
  WriteOut(data.data);
}

std::unique_ptr<ClientConnectionManager> P2pStream::MakeConnectionManager(
    ObjPtr<Cloud> const& cloud) {
  auto client_ptr = client_.Lock();
  assert(client_ptr);
  return std::make_unique<ClientConnectionManager>(
      cloud,
      client_ptr->server_connection_manager().GetServerConnectionFactory());
}

std::unique_ptr<CloudMessageStream> P2pStream::MakeDestinationStream(
    ClientConnectionManager& connection_manager) {
  return std::make_unique<CloudMessageStream>(action_context_,
                                              connection_manager);
}

}  // namespace ae
