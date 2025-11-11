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
#include "aether/client.h"
#include "aether/crypto/ikey_provider.h"
#include "aether/stream_api/protocol_gates.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/crypto/sync_crypto_provider.h"

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

class ClientCryptoProvider final : public ICryptoProvider {
 public:
  ClientCryptoProvider(Ptr<Client> const& client, ServerId server_id)
      : encryptor_{std::make_unique<ClientEncryptKeyProvider>(client,
                                                              server_id)},
        decryptor_{
            std::make_unique<ClientDecryptKeyProvider>(client, server_id)} {}

  IEncryptProvider* encryptor() override { return &encryptor_; }
  IDecryptProvider* decryptor() override { return &decryptor_; }

 private:
  SyncEncryptProvider encryptor_;
  SyncDecryptProvider decryptor_;
};

}  // namespace _internal

static constexpr std::size_t kBufferCapacity = 200;

ClientServerConnection::ClientServerConnection(ActionContext action_context,
                                               ObjPtr<Client> const& client,
                                               Server::ptr const& server)
    : action_context_{action_context},
      client_{client},
      server_{server},
      crypto_provider_{std::make_unique<_internal::ClientCryptoProvider>(
          client, server->server_id)},
      client_api_unsafe_{protocol_context_, *crypto_provider_->decryptor()},
      login_api_{protocol_context_, action_context_,
                 *crypto_provider_->encryptor()},
      channel_manager_{action_context_, server},
      channel_select_stream_{action_context_, channel_manager_},
      buffer_stream_{action_context_, kBufferCapacity},
      server_channel_{} {
  AE_TELED_DEBUG("Client server connection");

  Tie(buffer_stream_, channel_select_stream_);
  out_data_sub_ = buffer_stream_.out_data_event().Subscribe(
      MethodPtr<&ClientServerConnection::OutData>{this});

  StreamUpdate();
  SubscribeToSelectChannel();
}

void ClientServerConnection::Restream() { channel_select_stream_.Restream(); }

StreamInfo ClientServerConnection::stream_info() const {
  return buffer_stream_.stream_info();
}

ByteIStream::StreamUpdateEvent::Subscriber
ClientServerConnection::stream_update_event() {
  return buffer_stream_.stream_update_event();
}

ActionPtr<StreamWriteAction> ClientServerConnection::AuthorizedApiCall(
    SubApi<AuthorizedApi> auth_api) {
  auto client_ptr = client_.Lock();
  assert(client_ptr);
  auto api_call = ApiCallAdapter{ApiContext{login_api_}, buffer_stream_};
  api_call->login_by_alias(client_ptr->ephemeral_uid(), std::move(auth_api));
  return api_call.Flush();
}

ClientApiSafe& ClientServerConnection::client_safe_api() {
  return client_api_unsafe_.client_api_safe();
}

void ClientServerConnection::OutData(DataBuffer const& data) {
  auto parser = ApiParser{protocol_context_, data};
  parser.Parse(client_api_unsafe_);
}

void ClientServerConnection::SubscribeToSelectChannel() {
  stream_update_sub_ = channel_select_stream_.stream_update_event().Subscribe(
      MethodPtr<&ClientServerConnection::StreamUpdate>{this});
}

void ClientServerConnection::StreamUpdate() {
  if (channel_select_stream_.stream_info().link_state != LinkState::kLinked) {
    return;
  }

  auto const* channel = channel_select_stream_.server_channel();
  // check if channel is updated
  if (server_channel_ == channel) {
    return;
  }
  server_channel_ = channel;

  AE_TELED_DEBUG("Channel is linked, make new ping");
  Server::ptr server = server_.Lock();
  assert(server);
  // Create new ping if channel is updated
  // TODO: add ping interval config
  ping_ =
      OwnActionPtr<Ping>{action_context_, server, server_channel_->channel(),
                         *this, std::chrono::seconds{5}};

  ping_sub_ = ping_->StatusEvent().Subscribe(OnError{[this]() {
    AE_TELED_ERROR("Ping failed");
    channel_select_stream_.Restream();
  }});
}

}  // namespace ae
