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

#include "aether/server_connections/client_to_server_stream.h"

#include <utility>

#include "aether/memory.h"
#include "aether/client.h"

#include "aether/crypto/ikey_provider.h"
#include "aether/crypto/sync_crypto_provider.h"

#include "aether/methods/work_server_api/login_api.h"

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

ClientToServerStream::ClientToServerStream(ActionContext action_context,
                                           Ptr<Client> const& client,
                                           ServerId server_id)
    : action_context_{action_context},
      client_{client},
      server_id_{server_id},
      client_root_api_{protocol_context_},
      client_safe_api_{protocol_context_},
      login_api_{protocol_context_},
      authorized_api_{protocol_context_, action_context_} {
  AE_TELE_INFO(ClientServerStreamCreate, "Create ClientToServerStream");

  auto client_ptr = client_.Lock();

  client_auth_stream_.emplace(
      ProtocolReadGate{protocol_context_, client_safe_api_},
      DebugGate{
          Format("ClientToServerStreamGate server id {} client_uid {} \nwrite "
                 "{{}}",
                 server_id_, client_ptr->uid()),
          Format("ClientToServerStreamGate server id {} client_uid {} \nread "
                 "{{}}",
                 server_id_, client_ptr->uid())},
      CryptoGate{make_unique<SyncEncryptProvider>(
                     make_unique<_internal::ClientEncryptKeyProvider>(
                         client_ptr, server_id_)),
                 make_unique<SyncDecryptProvider>(
                     make_unique<_internal::ClientDecryptKeyProvider>(
                         client_ptr, server_id_))},
      // start streams with login by alias
      DelegateWriteInGate<DataBuffer>{
          Delegate{*this, MethodPtr<&ClientToServerStream::Login>{}}},
      EventWriteOutGate<DataBuffer>{
          EventSubscriber{client_root_api_.send_safe_api_data_event}},
      ProtocolReadGate{protocol_context_, client_root_api_});
}

ClientToServerStream::~ClientToServerStream() = default;

ActionPtr<StreamWriteAction> ClientToServerStream::Write(DataBuffer&& in_data) {
  auto info = client_auth_stream_->stream_info();
  if (in_data.size() > info.max_element_size) {
    AE_TELED_ERROR("Data size exceeds maximum element size");
    return ActionPtr<FailedStreamWriteAction>{action_context_};
  }
  return client_auth_stream_->Write(std::move(in_data));
}

ClientToServerStream::StreamUpdateEvent::Subscriber
ClientToServerStream::stream_update_event() {
  return client_auth_stream_->stream_update_event();
}

StreamInfo ClientToServerStream::stream_info() const {
  return client_auth_stream_->stream_info();
}

ClientToServerStream::OutDataEvent::Subscriber
ClientToServerStream::out_data_event() {
  return out_data_event_;
}

void ClientToServerStream::LinkOut(OutStream& out) {
  out_ = &out;
  Tie(*client_auth_stream_, *out_);
}

void ClientToServerStream::Unlink() { client_auth_stream_->Unlink(); }

ClientSafeApi& ClientToServerStream::client_safe_api() {
  return client_safe_api_;
}

AuthorizedApi& ClientToServerStream::authorized_api() {
  return authorized_api_;
}

ApiCallAdapter<AuthorizedApi> ClientToServerStream::authorized_api_adapter() {
  return ApiCallAdapter{ApiContext{protocol_context_, authorized_api_}, *this};
}

ProtocolContext& ClientToServerStream::protocol_context() {
  return protocol_context_;
}

DataBuffer ClientToServerStream::Login(DataBuffer&& data) {
  auto client_ptr = client_.Lock();
  auto api_context = ApiContext{protocol_context_, login_api_};
  api_context->login_by_alias(client_ptr->ephemeral_uid(), std::move(data));
  return DataBuffer{std::move(api_context)};
}

}  // namespace ae
