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
#include "aether/server_list/no_filter_server_list_policy.h"

#include "aether/tele/tele.h"

namespace ae {
ClientCloudConnection::ClientCloudConnection(
    ActionContext action_context, ObjPtr<Cloud> const& cloud,
    std::unique_ptr<IServerConnectionFactory>&& server_connection_factory)
    : action_context_{action_context},
      server_connection_selector_{cloud,
                                  make_unique<NoFilterServerListPolicy>(),
                                  std::move(server_connection_factory)} {
  Connect();
}

void ClientCloudConnection::Connect() {
  connection_selector_loop_ =
      AsyncForLoop<RcPtr<ClientServerConnection>>::Construct(
          server_connection_selector_,
          [this]() { return server_connection_selector_.GetConnection(); });

  SelectConnection();
}

std::unique_ptr<ByteStream> ClientCloudConnection::CreateStream(
    Uid destination_uid, StreamId stream_id) {
  AE_TELED_DEBUG("CreateStream destination uid {}, stream id {}",
                 destination_uid, static_cast<int>(stream_id));
  assert(server_connection_);

  auto gate_it = gates_.find(destination_uid);
  if (gate_it != gates_.end()) {
    return make_unique<OneGateStream>(
        gate_it->second->RegisterStream(stream_id));
  }

  auto& stream = server_connection_->GetStream(destination_uid);
  gate_it = gates_.emplace_hint(gate_it, destination_uid,
                                make_unique<SplitterGate>());
  Tie(*gate_it->second, stream);
  new_split_stream_subscription_.Push(
      gate_it->second->new_stream_event().Subscribe(
          [this, destination_uid](auto id, auto& stream) {
            new_stream_event_.Emit(destination_uid, id,
                                   make_unique<OneGateStream>(stream));
          }));
  return make_unique<OneGateStream>(gate_it->second->RegisterStream(stream_id));
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

  // TODO: add subscription to disconnection

  connection_status_subscription_ =
      server_connection_->server_stream()
          .in()
          .gate_update_event()
          .Subscribe([this]() {
            if (server_connection_->server_stream()
                    .in()
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
          *this, MethodPtr<&ClientCloudConnection::NewStream>{});
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

void ClientCloudConnection::NewStream(Uid uid, ByteStream& stream) {
  AE_TELED_DEBUG("New stream for uid {}", uid);

  auto gate_it = gates_.find(uid);
  if (gate_it != std::end(gates_)) {
    // retie existing stream
    Tie(*gate_it->second, stream);
    return;
  }

  // new stream created
  gate_it = gates_.emplace_hint(gate_it, uid, make_unique<SplitterGate>());
  Tie(*gate_it->second, stream);
  new_split_stream_subscription_.Push(
      gate_it->second->new_stream_event().Subscribe(
          [this, uid](auto id, auto& stream) {
            new_stream_event_.Emit(uid, id, make_unique<OneGateStream>(stream));
          }));
}

}  // namespace ae
