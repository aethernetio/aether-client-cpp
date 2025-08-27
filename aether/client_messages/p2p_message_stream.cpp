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

#include "aether/client_messages/client_messages_tele.h"

namespace ae {
P2pStream::P2pStream(ActionContext action_context, Ptr<Client> const& client,
                     Uid destination)
    : action_context_{action_context},
      client_{client},
      destination_{destination},
      receive_client_connection_{client->client_connection()},
      // TODO: add buffer config
      buffer_stream_{action_context_, 20 * 1024} {
  AE_TELE_DEBUG(kP2pMessageStreamNew, "P2pStream created for {}", destination_);
  // destination uid must not be empty
  assert(!destination_.empty());

  ConnectReceive();
  ConnectSend();
}

P2pStream::P2pStream(ActionContext action_context, Ptr<Client> const& client,
                     Uid destination,
                     std::unique_ptr<ByteIStream> receive_stream)
    : action_context_{action_context},
      client_{client},
      destination_{destination},
      receive_client_connection_{client->client_connection()},
      // TODO: add buffer config
      buffer_stream_{action_context_, 20 * 1024},
      receive_stream_{std::move(receive_stream)} {
  AE_TELE_DEBUG(kP2pMessageStreamRec, "P2pStream received for {}",
                destination_);
  // destination uid must not be empty
  assert(!destination_.empty());

  out_data_sub_ = receive_stream_->out_data_event().Subscribe(
      out_data_event_, MethodPtr<&OutDataEvent::Emit>{});
  ConnectSend();
}

P2pStream::~P2pStream() {
  if (receive_client_connection_) {
    receive_client_connection_->CloseStream(destination_);
  }
  if (send_client_connection_) {
    send_client_connection_->CloseStream(destination_);
  }
}

ActionPtr<StreamWriteAction> P2pStream::Write(DataBuffer&& data) {
  return buffer_stream_.Write(std::move(data));
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

void P2pStream::ConnectReceive() {
  receive_stream_ = receive_client_connection_->CreateStream(destination_);
  out_data_sub_ = receive_stream_->out_data_event().Subscribe(
      out_data_event_, MethodPtr<&OutDataEvent::Emit>{});
}

void P2pStream::ConnectSend() {
  if (!send_client_connection_) {
    auto client_ptr = client_.Lock();

    // get destination client's cloud connection  to creat stream
    auto get_client_connection_action =
        client_ptr->client_connection_manager()->GetClientConnection(
            destination_);
    get_client_connection_subscription_ =
        get_client_connection_action->StatusEvent().Subscribe(
            OnResult{[this](auto& action) {
              send_client_connection_ = action.client_cloud_connection();
              TieSendStream(*send_client_connection_);
            }});
    return;
  }
  TieSendStream(*send_client_connection_);
}

void P2pStream::TieSendStream(ClientConnection& client_connection) {
  send_stream_ = client_connection.CreateStream(destination_);
  AE_TELED_DEBUG("Send tied");
  Tie(buffer_stream_, *send_stream_);
}
}  // namespace ae
