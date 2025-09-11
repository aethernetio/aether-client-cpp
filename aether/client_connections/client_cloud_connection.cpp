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

#include "aether/client_connections/client_connections_tele.h"
#include "aether/connection_manager/iserver_priority_policy.h"

namespace ae {
namespace client_cloud_connection_internal {
class ServerPriority final : public IServerPriorityPolicy {
 public:
  bool Filter(ObjPtr<Server> const&) override { return false; }

  bool Compare(ObjPtr<Server> const& left,
               ObjPtr<Server> const& right) override {
    return left->server_id < right->server_id;
  }
};
static ServerPriority server_priority{};
}  // namespace client_cloud_connection_internal

ClientCloudConnection::ClientCloudConnection(
    ActionContext action_context, ObjPtr<Cloud> const& cloud,
    std::unique_ptr<IServerConnectionFactory>&& server_connection_factory)
    : action_context_{action_context},
      server_connection_selector_{
          cloud, client_cloud_connection_internal::server_priority,
          std::move(server_connection_factory)} {
  Connect();
}

std::unique_ptr<ByteIStream> ClientCloudConnection::CreateStream(
    Uid destination_uid) {
  AE_TELE_DEBUG(CloudClientConnStreamCreate, "CreateStream destination uid {}",
                destination_uid);
  assert(server_connection_);

  auto stream_it = streams_.find(destination_uid);
  if (stream_it == std::end(streams_)) {
    auto& stream = server_connection_->GetStream(destination_uid);
    bool ok;
    std::tie(stream_it, ok) =
        streams_.emplace(destination_uid, make_unique<ByteStream>());
    Tie(*stream_it->second, stream);
  }

  auto ret_stream = make_unique<ByteStream>();
  Tie(*ret_stream, *stream_it->second);
  return ret_stream;
}

ClientCloudConnection::NewStreamEvent::Subscriber
ClientCloudConnection::new_stream_event() {
  return new_stream_event_;
}

void ClientCloudConnection::CloseStream(Uid uid) {
  AE_TELE_DEBUG(CloudClientConnStreamClose, "Close stream uid {}", uid);
  auto it = streams_.find(uid);
  if (it == std::end(streams_)) {
    return;
  }
  streams_.erase(it);
  server_connection_->CloseStream(uid);
}

void ClientCloudConnection::SendTelemetry() {
  if (!server_connection_) {
    assert(false);
    return;
  }
  server_connection_->SendTelemetry();
}

RcPtr<ClientServerConnection> ClientCloudConnection::TopConnection() {
  return server_connection_;
}

bool ClientCloudConnection::Rotate() {
  AE_TELED_DEBUG("Rotate connection");
  if (server_connection_) {
    server_connection_->NextChannel();
    return true;
  }
  return false;
}

void ClientCloudConnection::Connect() {
  connection_selector_loop_ =
      AsyncForLoop<RcPtr<ClientServerConnection>>::Construct(
          server_connection_selector_);

  SelectConnection();
}

void ClientCloudConnection::SelectConnection() {
  if (server_connection_ = connection_selector_loop_->Update();
      !server_connection_) {
    AE_TELED_ERROR("Server channel list is ended");
    ServerListEnded();
    return;
  }

  // subscribe to server error
  server_error_sub_ = server_connection_->server_error_event().Subscribe(
      [this]() { OnConnectionError(); });

  // restore all known streams to a new server
  for (auto& [uid, stream] : streams_) {
    Tie(*stream, server_connection_->GetStream(uid));
  }

  new_stream_event_subscription_ =
      server_connection_->new_stream_event().Subscribe(
          *this, MethodPtr<&ClientCloudConnection::NewStream>{});
}

void ClientCloudConnection::OnConnectionError() {
  AE_TELED_ERROR("Connection error");
  AE_TELED_DEBUG("Reconnect");
  SelectConnection();
}

void ClientCloudConnection::ServerListEnded() {
  next_server_loop_timer_ = ActionPtr<NextServerLoopTimer>{
      action_context_, std::chrono::milliseconds{5000}};
  next_server_loop_subs_ =
      next_server_loop_timer_->StatusEvent().Subscribe(OnResult{[&]() {
        AE_TELED_DEBUG("Connect again");
        Connect();
      }});
}

void ClientCloudConnection::NewStream(Uid uid, ByteIStream& stream) {
  AE_TELE_DEBUG(CloudClientNewStream, "New stream for uid {}", uid);

  auto stream_it = streams_.find(uid);
  if (stream_it != std::end(streams_)) {
    // retie existing stream
    Tie(*stream_it->second, stream);
    return;
  }

  // new stream created
  std::tie(stream_it, std::ignore) =
      streams_.emplace(uid, make_unique<ByteStream>());
  Tie(*stream_it->second, stream);
  auto ret_stream = make_unique<ByteStream>();
  Tie(*ret_stream, *stream_it->second);
  new_stream_event_.Emit(uid, std::move(ret_stream));
}

}  // namespace ae
