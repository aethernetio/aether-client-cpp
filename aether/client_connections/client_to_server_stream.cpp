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

#include "aether/client_connections/client_connections_tele.h"

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

ClientToServerStream::ClientToServerStream(
    ActionContext action_context, Ptr<Client> const& client, ServerId server_id,
    std::unique_ptr<ByteStream> server_stream)
    : action_context_{action_context},
      client_{client},
      server_id_{server_id},
      client_root_api_{protocol_context_},
      client_safe_api_{protocol_context_},
      login_api_{protocol_context_},
      authorized_api_{protocol_context_, action_context_} {
  AE_TELE_INFO(ClientServerStreamCreate, "Create ClientToServerStreamGate");

  auto client_ptr = client_.Lock();

  client_auth_stream_.emplace(
      ProtocolReadGate{protocol_context_, client_safe_api_},
      DebugGate{
          Format("ClientToServerStreamGate server id {} client_uid {} \nwrite "
                 "{{}}",
                 server_id_, client_ptr->uid()),
          Format(
              "ClientToServerStreamGate server id {} client_uid {} \nread {{}}",
              server_id_, client_ptr->uid())},
      CryptoGate{make_unique<SyncEncryptProvider>(
                     make_unique<_internal::ClientEncryptKeyProvider>(
                         client_ptr, server_id_)),
                 make_unique<SyncDecryptProvider>(
                     make_unique<_internal::ClientDecryptKeyProvider>(
                         client_ptr, server_id_))},
      // start streams with login by alias
      ProtocolWriteMessageGate<DataBuffer>{
          protocol_context_,
          [&, alias{client_ptr->ephemeral_uid()}](
              ProtocolContext& context, DataBuffer&& data) -> DataBuffer {
            auto api_context = ApiContext{context, login_api_};
            api_context->login_by_alias(alias, std::move(data));
            return DataBuffer{std::move(api_context)};
          }},
      EventSubscribeGate<DataBuffer>{
          EventSubscriber{client_root_api_.send_safe_api_data_event}},
      ProtocolReadGate{protocol_context_, client_root_api_},
      std::move(server_stream));
}

ClientToServerStream::~ClientToServerStream() = default;

ClientToServerStream::InGate& ClientToServerStream::in() {
  assert(client_auth_stream_);
  return client_auth_stream_->in();
}

void ClientToServerStream::LinkOut(OutGate& /* out */) { assert(false); }

ProtocolContext& ClientToServerStream::protocol_context() {
  return protocol_context_;
}

ClientSafeApi& ClientToServerStream::client_safe_api() {
  return client_safe_api_;
}

AuthorizedApi& ClientToServerStream::authorized_api() {
  return authorized_api_;
}

}  // namespace ae
