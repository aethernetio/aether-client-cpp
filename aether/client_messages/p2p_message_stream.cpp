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
      buffer_stream_{action_context, 20 * 1024},
      send_receive_stream_{} {
  AE_TELE_DEBUG(kP2pMessageStreamNew, "P2pStream created for {}", destination_);
  // connect buffered gate and send_receive gate
  Tie(buffer_stream_, send_receive_stream_);

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
      buffer_stream_{action_context, 100},
      send_receive_stream_{},
      receive_stream_{std::move(receive_stream)} {
  AE_TELE_DEBUG(kP2pMessageStreamRec, "P2pStream received for {}",
                destination_);
  // connect buffered gate and send_receive gate
  Tie(buffer_stream_, send_receive_stream_);
  // connect receive stream immediately
  send_receive_stream_.LinkReadStream(*receive_stream_);
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

ActionView<StreamWriteAction> P2pStream::Write(DataBuffer&& data) {
  return buffer_stream_.Write(std::move(data));
}

P2pStream::StreamUpdateEvent::Subscriber P2pStream::stream_update_event() {
  return buffer_stream_.stream_update_event();
}

StreamInfo P2pStream::stream_info() const {
  return buffer_stream_.stream_info();
}

P2pStream::OutDataEvent::Subscriber P2pStream::out_data_event() {
  return buffer_stream_.out_data_event();
}

void P2pStream::ConnectReceive() {
  receive_stream_ = receive_client_connection_->CreateStream(destination_);
  send_receive_stream_.LinkReadStream(*receive_stream_);
}

void P2pStream::ConnectSend() {
  if (!send_client_connection_) {
    auto client_ptr = client_.Lock();

    // get destination client's cloud connection  to creat stream
    auto get_client_connection_action =
        client_ptr->client_connection_manager()->GetClientConnection(
            destination_);
    get_client_connection_subscription_ =
        get_client_connection_action->ResultEvent().Subscribe(
            [this](auto& action) {
              send_client_connection_ = action.client_cloud_connection();
              TieSendStream(*send_client_connection_);
            });
    return;
  }
  TieSendStream(*send_client_connection_);
}

void P2pStream::TieSendStream(ClientConnection& client_connection) {
  send_stream_ = client_connection.CreateStream(destination_);
  AE_TELED_DEBUG("Send tied");
  send_receive_stream_.LinkWriteStream(*send_stream_);
}
}  // namespace ae
