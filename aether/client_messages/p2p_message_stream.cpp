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
                     Uid destination, StreamId stream_id)
    : action_context_{action_context},
      client_{client},
      destination_{destination},
      stream_id_{stream_id},
      receive_client_connection_{client->client_connection()},
      // TODO: add buffer config
      buffer_gate_{action_context, 20 * 1024},
      send_receive_gate_{WriteOnlyGate{}, ReadOnlyGate{action_context_}} {
  AE_TELE_DEBUG(kP2pMessageStreamNew, "P2pStream {} created for {}",
                static_cast<int>(stream_id_), destination_);
  // connect buffered gate and send_receive gate
  Tie(buffer_gate_, send_receive_gate_);

  ConnectReceive();
  ConnectSend();
}

P2pStream::P2pStream(ActionContext action_context, Ptr<Client> const& client,
                     Uid destination, StreamId stream_id,
                     std::unique_ptr<ByteStream> receive_stream)
    : action_context_{action_context},
      client_{client},
      destination_{destination},
      stream_id_{stream_id},
      receive_client_connection_{client->client_connection()},
      // TODO: add buffer config
      buffer_gate_{action_context, 100},
      send_receive_gate_{WriteOnlyGate{}, ReadOnlyGate{action_context_}},
      receive_stream_{std::move(receive_stream)} {
  AE_TELE_DEBUG(kP2pMessageStreamRec, "P2pStream received {} for {}",
                static_cast<int>(stream_id_), destination_);
  // connect buffered gate and send_receive gate
  Tie(buffer_gate_, send_receive_gate_);
  // connect receive stream immediately
  Tie(send_receive_gate_.get_read_gate(), *receive_stream_);

  ConnectSend();
}

P2pStream::~P2pStream() {
  if (receive_client_connection_) {
    receive_client_connection_->CloseStream(destination_, stream_id_);
  }
  if (send_client_connection_) {
    send_client_connection_->CloseStream(destination_, stream_id_);
  }
}

P2pStream::InGate& P2pStream::in() { return buffer_gate_; }

void P2pStream::LinkOut(OutGate& /* out */) { assert(false); }

void P2pStream::ConnectReceive() {
  receive_stream_ =
      receive_client_connection_->CreateStream(destination_, stream_id_);
  Tie(send_receive_gate_.get_read_gate(), *receive_stream_);
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
  send_stream_ = client_connection.CreateStream(destination_, stream_id_);
  AE_TELED_DEBUG("Send tied");
  Tie(send_receive_gate_.get_write_gate(), *send_stream_);
}
}  // namespace ae
