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

#include "aether/client_connections/client_to_server_stream.h"

#include <utility>

#include "aether/client.h"

#include "aether/crypto/ikey_provider.h"
#include "aether/crypto/sync_crypto_provider.h"

#include "aether/stream_api/debug_gate.h"
#include "aether/stream_api/stream_api.h"
#include "aether/stream_api/tied_stream.h"
#include "aether/stream_api/crypto_stream.h"
#include "aether/stream_api/protocol_stream.h"
#include "aether/stream_api/transport_write_gate.h"

#include "aether/methods/work_server_api/login_api.h"
#include "aether/methods/client_api/client_safe_api.h"
#include "aether/methods/work_server_api/authorized_api.h"

#include "aether/tele/tele.h"

namespace ae {
namespace _internal {
class ClientKeyProvider : public ISyncKeyProvider {
 public:
  explicit ClientKeyProvider(Ptr<Client> const& client, ServerId server_id)
      : client_{client}, server_id_{server_id} {}

  CryptoNonce const& Nonce() const override {
    auto client_ptr = client_.Lock();
    assert(client_ptr);
    auto* server_key = client_ptr->server_state(server_id_);
    assert(server_key);
    server_key->Next();
    return server_key->nonce();
  }

 protected:
  PtrView<Client> client_;
  ServerId server_id_;
};

class ClientEncryptKeyProvider : public ClientKeyProvider {
 public:
  using ClientKeyProvider::ClientKeyProvider;

  Key GetKey() const override {
    auto client_ptr = client_.Lock();
    assert(client_ptr);
    auto const* server_key = client_ptr->server_state(server_id_);
    assert(server_key);

    return server_key->client_to_server();
  }
};

class ClientDecryptKeyProvider : public ClientKeyProvider {
 public:
  using ClientKeyProvider::ClientKeyProvider;

  Key GetKey() const override {
    auto client_ptr = client_.Lock();
    assert(client_ptr);
    auto const* server_key = client_ptr->server_state(server_id_);
    assert(server_key);

    return server_key->server_to_client();
  }
};
}  // namespace _internal

ClientToServerStream::ClientToServerStream(ActionContext action_context,
                                           Ptr<Client> client,
                                           ServerId server_id,
                                           Ptr<ITransport> server_transport)
    : action_context_{action_context},
      client_{std::move(client)},
      server_id_{server_id},
      server_transport_{std::move(server_transport)} {
  AE_TELED_DEBUG("Create ClientToServerStreamGate");
  connection_success_subscription_ =
      server_transport_->ConnectionSuccess().Subscribe(
          [this]() { OnConnected(); });
  connection_failed_subscription_ =
      server_transport_->ConnectionError().Subscribe(
          [this]() { OnDisconnected(); });

  server_transport_->Connect();
  InitStreams();
}

ClientToServerStream::~ClientToServerStream() {
  AE_TELED_DEBUG("Destroy ClientToServerStreamGate");
}

ClientToServerStream::InGate& ClientToServerStream::in() {
  assert(server_stream_);
  return server_stream_->in();
}

void ClientToServerStream::LinkOut(OutGate& /* out */) { assert(false); }

EventSubscriber<void()> ClientToServerStream::connected_event() {
  return connected_event_;
}
EventSubscriber<void()> ClientToServerStream::connection_error_event() {
  return connection_error_event_;
}

ConnectionState ClientToServerStream::connection_state() const {
  return server_transport_->GetConnectionInfo().connection_state;
}

void ClientToServerStream::OnConnected() {
  AE_TELED_INFO("Connected to server");
  connected_event_.Emit();
}

void ClientToServerStream::OnDisconnected() {
  AE_TELED_INFO("Server connection lost");
  connection_error_event_.Emit();
}

void ClientToServerStream::InitStreams() {
  AE_TELED_INFO("Make server stream");

  auto stream_id = StreamIdGenerator::GetNextClientStreamId();

  server_stream_.emplace(
      DebugGate{
          Format(
              "ClientToServerStreamGate server id {} client_uid {} \nwrite {}",
              server_id_, client_->uid()),
          Format(
              "ClientToServerStreamGate server id {} client_uid {} \nread {}",
              server_id_, client_->uid())},
      CryptoGate{MakePtr<SyncEncryptProvider>(
                     MakePtr<_internal::ClientEncryptKeyProvider>(client_,
                                                                  server_id_)),
                 MakePtr<SyncDecryptProvider>(
                     MakePtr<_internal::ClientDecryptKeyProvider>(client_,
                                                                  server_id_))},
      StreamApiGate{protocol_context_, stream_id},
      // start streams with login by uid
      ProtocolWriteGate{protocol_context_, LoginApi{},
                        LoginApi::LoginByUid{{}, stream_id, client_->uid()}},
      ProtocolReadGate{protocol_context_, ClientSafeApi{}},
      TransportWriteGate{action_context_, server_transport_});

  // write something to init the stream
  AE_TELED_DEBUG("Send authorization ping");
  server_stream_->in().Write(
      PacketBuilder{protocol_context_,
                    PackMessage{AuthorizedApi{}, AuthorizedApi::Ping{}}},
      TimePoint::clock::now());
}

}  // namespace ae
