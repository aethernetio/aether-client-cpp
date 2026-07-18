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

#include "aether/aether.h"
#include "aether/server.h"
#include "aether/client.h"
#include "aether/crypto/ikey_provider.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/stream_api/api_call_adapter.h"
#include "aether/crypto/sync_crypto_provider.h"

#include "aether/tele/tele.h"

namespace ae {
namespace client_server_connection_internal {
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

BufferedServerConnection::BufferedServerConnection(AeContext const& ae_context,
                                                   Ptr<Server> const& server)
    : buffer_write{ae_context,
                   [&](DataBuffer&& in_data) -> WriteAction* {
                     if (server_connection.stream_info().is_writable) {
                       return &server_connection.Write(std::move(in_data));
                     }
                     return nullptr;
                   }},
      server_connection{ae_context, server} {
  if (server_connection.stream_info().is_writable) {
    buffer_write.buffer_off();
  }
  server_connection.stream_update_event().Subscribe([&]() {
    if (server_connection.stream_info().is_writable) {
      if (buffer_write.is_buffer_on()) {
        buffer_write.buffer_off();
      }
    } else {
      if (!buffer_write.is_buffer_on()) {
        buffer_write.buffer_on();
      }
    }
  });
}

WriteAction& BufferedServerConnection::Write(DataBuffer&& in_data) {
  return buffer_write.Write(std::move(in_data));
}

BufferedServerConnection::StreamUpdateEvent::Subscriber
BufferedServerConnection::stream_update_event() {
  return server_connection.stream_update_event();
}

StreamInfo BufferedServerConnection::stream_info() const {
  return server_connection.stream_info();
}

BufferedServerConnection::OutDataEvent::Subscriber
BufferedServerConnection::out_data_event() {
  return server_connection.out_data_event();
}
void BufferedServerConnection::Restream() { server_connection.Restream(); }

}  // namespace client_server_connection_internal

ClientServerConnection::ClientServerConnection(AeContext const& ae_context,
                                               Ptr<Client> const& client,
                                               Ptr<Server> const& server)
    : ae_context_{ae_context},
      client_{client},
      server_{server},
      ephemeral_uid_{client->ephemeral_uid()},
      crypto_provider_{std::make_unique<
          client_server_connection_internal::ClientCryptoProvider>(
          client, server->server_id)},
      client_api_unsafe_{protocol_context_, *crypto_provider_->decryptor()},
      login_api_{protocol_context_, *crypto_provider_->encryptor()},
      server_connection_{ae_context_, server} {
  AE_TELED_DEBUG("Client server connection from {} to {}", ephemeral_uid_,
                 server->server_id);

  server_connection_.out_data_event().Subscribe(
      MethodPtr<&ClientServerConnection::OutData>{this});
}

ClientServerConnection::~ClientServerConnection() {
  auto server = server_.Lock();
  assert(server);
  AE_TELED_DEBUG("Destroy client server connection from {} to {}",
                 ephemeral_uid_, server->server_id);
}

void ClientServerConnection::Restream() { server_connection_.Restream(); }

StreamInfo ClientServerConnection::stream_info() const {
  return server_connection_.stream_info();
}

ByteIStream::StreamUpdateEvent::Subscriber
ClientServerConnection::stream_update_event() {
  return server_connection_.stream_update_event();
}

WriteAction& ClientServerConnection::LoginApiCall(SubApi<LoginApi> login_api) {
  auto packet = login_api(login_api_);
  return server_connection_.Write(std::move(packet));
}

WriteAction& ClientServerConnection::AuthorizedApiCall(
    SubApi<AuthorizedApi> auth_api) {
  auto api_call = ApiCallAdapter{ApiContext{login_api_}, server_connection_};
  api_call->login_by_alias(ephemeral_uid_, std::move(auth_api));
  // cppcheck reports false positive
  // cppcheck-suppress returnReference
  return api_call.Flush();
}

ClientApiSafe& ClientServerConnection::client_safe_api() {
  return client_api_unsafe_.client_api_safe();
}

ServerConnection& ClientServerConnection::server_connection() {
  return server_connection_.server_connection;
}

std::optional<prepared_packet::PreparedSendMessageBlock>
ClientServerConnection::ExportPreparedSendMessageBlock(
    Uid target_uid, std::uint32_t reserve_nonce_count) {
  auto client = client_.Lock();
  auto server = server_.Lock();

  if ((client == nullptr) || (server == nullptr) ||
      (reserve_nonce_count == 0)) {
    return std::nullopt;
  }

  std::optional<prepared_packet::PreparedEndpoint> prepared_endpoint;

  for (auto const& e : server->endpoints) {
    if (e.protocol != Protocol::kUdp) {
      continue;
    }

    auto endpoint = prepared_packet::MakePreparedEndpoint(e);
    if (endpoint) {
      prepared_endpoint = *endpoint;
      break;
    }
  }

  if (!prepared_endpoint) {
    return std::nullopt;
  }

  auto* server_key = client->server_state(server->server_id);
  if (server_key == nullptr) {
    return std::nullopt;
  }

  prepared_packet::PreparedSendMessageBlock block;
  block.endpoint = *prepared_endpoint;
  block.sender_ephemeral_uid = ephemeral_uid_;
  block.target_uid = target_uid;
  block.client_to_server_key = server_key->client_to_server();

  // Store current nonce. EncodePacket() will call Next() before encryption,
  // matching existing ClientKeyProvider behavior.
  block.next_nonce = server_key->nonce();
  block.nonce_left = reserve_nonce_count;

  // Burn/reserve the same nonce range in the full client,
  // so the normal Aether path cannot reuse it later.
  for (std::uint32_t i = 0; i < reserve_nonce_count; ++i) {
    server_key->Next();
  }

  return block;
}

void ClientServerConnection::OutData(DataBuffer const& data) {
  auto parser = ApiParser{protocol_context_, data};
  parser.Parse(client_api_unsafe_);
}

}  // namespace ae
