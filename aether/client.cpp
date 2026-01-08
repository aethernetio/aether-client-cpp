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
#include "aether/work_cloud.h"

#include "aether/tele/tele.h"

namespace ae {

Client::Client(Aether& aether, std::string id, Domain* domain)
    : Obj{domain}, aether_{&aether}, id_{std::move(id)} {
  AE_TELED_DEBUG("Created client id={}", id_);
  domain_->Load(*this, Hash(kTypeName, id_));
  if (uid_.empty()) {
    return;
  }

  AE_TELED_DEBUG("Loaded client id={}, uid={}", id_, uid_);
  Init(std::make_unique<WorkCloud>(*aether_, uid_, domain_));
}

std::string const& Client::id() const { return client_id_; }
Uid const& Client::parent_uid() const { return parent_uid_; }
Uid const& Client::uid() const { return uid_; }
Uid const& Client::ephemeral_uid() const { return ephemeral_uid_; }

ServerKeys* Client::server_state(ServerId server_id) {
  auto ss_it = server_keys_.find(server_id);
  if (ss_it == server_keys_.end()) {
    return nullptr;
  }
  return &ss_it->second;
}

Cloud& Client::cloud() { return *cloud_; }

ClientCloudManager& Client::cloud_manager() { return *client_cloud_manager_; }

ServerConnectionManager& Client::server_connection_manager() {
  return *server_connection_manager_;
}

ClientConnectionManager& Client::connection_manager() {
  return *client_connection_manager_;
}

CloudConnection& Client::cloud_connection() { return *cloud_connection_; }

P2pMessageStreamManager& Client::message_stream_manager() {
  return *message_stream_manager_;
}

void Client::SetConfig(Uid parent_uid, Uid uid, Uid ephemeral_uid,
                       Key master_key, std::unique_ptr<Cloud> c) {
  parent_uid_ = parent_uid;
  uid_ = uid;
  ephemeral_uid_ = ephemeral_uid;
  master_key_ = std::move(master_key);
  for (auto& s : c->servers()) {
    server_keys_.emplace(s->server_id, ServerKeys{s->server_id, master_key_});
  }

  Init(std::move(c));

  // Save the config
  domain_->Save(*this, Hash(kTypeName, id_));
}

void Client::SendTelemetry() {
#if AE_TELE_ENABLED
  telemetry_->SendTelemetry();
#endif
}

void Client::Init(std::unique_ptr<Cloud> cloud) {
  cloud_ = std::move(cloud);
  client_cloud_manager_ =
      std::make_unique<ClientCloudManager>(*aether_, *this, domain_);
  server_connection_manager_ =
      std::make_unique<ServerConnectionManager>(*aether_, *this);

  client_connection_manager_ = std::make_unique<ClientConnectionManager>(
      *cloud_, server_connection_manager_->GetServerConnectionFactory());
  cloud_connection_ = std::make_unique<CloudConnection>(
      *aether_, connection_manager(), AE_CLOUD_MAX_SERVER_CONNECTIONS);
  message_stream_manager_ =
      std::make_unique<P2pMessageStreamManager>(*aether_, *this);
#if AE_TELE_ENABLED
  // also create telemetry
  telemetry_ = ActionPtr<Telemetry>(ActionContext{*aether_}, *aether_,
                                    *cloud_connection_);
#endif
}

}  // namespace ae
