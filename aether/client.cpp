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

#include "aether/client.h"

#include <utility>

#include "aether/aether.h"

namespace ae {

#ifdef AE_DISTILLATION
Client::Client(Aether::ptr aether, Domain* domain)
    : Base(domain), aether_{std::move(aether)} {}
#endif  // AE_DISTILLATION

Uid const& Client::uid() const { return uid_; }
Uid const& Client::ephemeral_uid() const { return ephemeral_uid_; }
ServerKeys* Client::server_state(ServerId server_id) {
  auto ss_it = server_keys_.find(server_id);
  if (ss_it == server_keys_.end()) {
    return nullptr;
  }
  return &ss_it->second;
}

Cloud::ptr const& Client::cloud() const { return cloud_; }

ClientCloudManager::ptr const& Client::cloud_manager() const {
  assert(client_cloud_manager_);
  return client_cloud_manager_;
}

ServerConnectionManager& Client::server_connection_manager() {
  if (!server_connection_manager_) {
    auto aether = Aether::ptr{aether_};
    server_connection_manager_ = std::make_unique<ServerConnectionManager>(
        *aether, aether, MakePtrFromThis(this));
  }
  return *server_connection_manager_;
}

ClientConnectionManager& Client::connection_manager() {
  if (!client_connection_manager_) {
    auto aether = Aether::ptr{aether_};
    client_connection_manager_ = std::make_unique<ClientConnectionManager>(
        cloud_, server_connection_manager().GetServerConnectionFactory());
  }
  return *client_connection_manager_;
}

CloudMessageStream& Client::cloud_message_stream() {
  if (!cloud_message_stream_) {
    auto aether = Aether::ptr{aether_};
    cloud_message_stream_ =
        std::make_unique<CloudMessageStream>(*aether, connection_manager());
  }
  return *cloud_message_stream_;
}

P2pMessageStreamManager& Client::message_stream_manager() {
  if (!message_stream_manager_) {
    auto aether = Aether::ptr{aether_};
    message_stream_manager_ = std::make_unique<P2pMessageStreamManager>(
        *aether, MakePtrFromThis(this));
  }
  return *message_stream_manager_;
}

void Client::SetConfig(Uid uid, Uid ephemeral_uid, Key master_key,
                       Cloud::ptr cloud) {
  uid_ = uid;
  ephemeral_uid_ = ephemeral_uid;
  master_key_ = std::move(master_key);
  cloud_ = std::move(cloud);

  for (auto& s : cloud_->servers()) {
    server_keys_.emplace(s->server_id, ServerKeys{s->server_id, master_key_});
  }

  client_cloud_manager_ = domain_->CreateObj<ClientCloudManager>(
      ObjPtr<Aether>{aether_}, MakePtrFromThis(this));
}

}  // namespace ae
