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

#include "aether/client_connections/client_cloud_connection.h"

#include "aether/stream_api/one_gate_stream.h"

#include "aether/tele/tele.h"

namespace ae {
ClientCloudConnection::ClientCloudConnection(
    ActionContext action_context,
    Ptr<ServerConnectionSelector> client_server_connection_selector)
    : action_context_{action_context},
      server_connection_selector_{
          std::move(client_server_connection_selector)} {
  Connect();
}

void ClientCloudConnection::Connect() {
  connection_selector_loop_ = MakePtr<AsyncForLoop>(
      AsyncForLoop<Ptr<ClientServerConnection>>::Construct(
          *server_connection_selector_,
          [this]() { return server_connection_selector_->GetConnection(); }));

  SelectConnection();
}

Ptr<ByteStream> ClientCloudConnection::CreateStream(Uid destination_uid,
                                                    StreamId stream_id) {
  AE_TELED_DEBUG("CreateStream destination uid {}, stream id {}",
                 destination_uid, static_cast<int>(stream_id));
  assert(server_connection_);

  auto gate_it = gates_.find(destination_uid);
  if (gate_it != gates_.end()) {
    return MakePtr<OneGateStream>(gate_it->second->RegisterStream(stream_id));
  }

  auto& stream = server_connection_->GetStream(destination_uid);
  gate_it =
      gates_.emplace_hint(gate_it, destination_uid, MakePtr<SplitterGate>());
  Tie(*gate_it->second, stream);
  new_split_stream_subscription_.Push(
      gate_it->second->new_stream_event().Subscribe(
          [this, destination_uid](auto id, auto& stream) {
            new_stream_event_.Emit(destination_uid, id,
                                   MakePtr<OneGateStream>(stream));
          }));
  return MakePtr<OneGateStream>(gate_it->second->RegisterStream(stream_id));
}

ClientCloudConnection::NewStreamEvent::Subscriber
ClientCloudConnection::new_stream_event() {
  return new_stream_event_;
}

void ClientCloudConnection::CloseStream(Uid uid, StreamId stream_id) {
  AE_TELED_DEBUG("Close stream uid {}, stream id {}", uid,
                 static_cast<int>(stream_id));
  auto it = gates_.find(uid);
  if (it == std::end(gates_)) {
    return;
  }

  it->second->CloseStream(stream_id);
  if (it->second->stream_count() == 0) {
    gates_.erase(it);
    server_connection_->CloseStream(uid);
  }
}

void ClientCloudConnection::SelectConnection() {
  if (server_connection_ = connection_selector_loop_->Update();
      !server_connection_) {
    AE_TELED_ERROR("Server channel list is ended");
    return;
  }

  connection_status_subscription_ =
      server_connection_->server_stream()
          ->in()
          .gate_update_event()
          .Subscribe([this]() {
            if (server_connection_->server_stream()
                    ->in()
                    .stream_info()
                    .is_linked) {
              AE_TELED_INFO("Client cloud connection is connected");
            } else {
              OnConnectionError();
            }
          })
          .Once();

  // restore all known streams to a new server
  for (auto& [uid, gate] : gates_) {
    Tie(*gate, server_connection_->GetStream(uid));
  }

  new_stream_event_subscription_ =
      server_connection_->new_stream_event().Subscribe(
          [this](auto uid, auto stream) { NewStream(uid, std::move(stream)); });
}

void ClientCloudConnection::OnConnectionError() {
  AE_TELED_ERROR("Connection error");
  reconnect_notify_ = ReconnectNotify{action_context_};
  reconnect_notify_subscription_ = reconnect_notify_
                                       .SubscribeOnResult([this](auto const&) {
                                         AE_TELED_DEBUG("Reconnect");
                                         SelectConnection();
                                       })
                                       .Once();
  reconnect_notify_.Notify();
}

void ClientCloudConnection::NewStream(Uid uid, Ptr<ByteStream> stream) {
  AE_TELED_DEBUG("New stream for uid {}", uid);

  auto gate_it = gates_.find(uid);
  if (gate_it != std::end(gates_)) {
    // retie existing stream
    Tie(*gate_it->second, *stream);
    return;
  }

  // new stream created
  gate_it = gates_.emplace_hint(gate_it, uid, MakePtr<SplitterGate>());
  Tie(*gate_it->second, *stream);
  new_split_stream_subscription_.Push(
      gate_it->second->new_stream_event().Subscribe(
          [this, uid](auto id, auto& stream) {
            new_stream_event_.Emit(uid, id, MakePtr<OneGateStream>(stream));
          }));
}

}  // namespace ae
