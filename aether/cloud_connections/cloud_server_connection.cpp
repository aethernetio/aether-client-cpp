/*
 * Copyright 2025 Aethernet Inc.
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

#include "aether/cloud_connections/cloud_server_connection.h"

#include <cassert>
#include <cstdint>
#include <limits>

#include "aether/server.h"

namespace ae {

CloudServerConnection::CloudServerConnection(
    Ptr<Cloud> const& cloud, ServerId server_id,
    IServerConnectionFactory& connection_factory)
    : cloud_{cloud},
      server_id_{server_id},
      connection_factory_{&connection_factory} {}

void CloudServerConnection::Restream() {
  if (client_connection_) {
    client_connection_->Restream();
  }
}

bool CloudServerConnection::quarantine() const { return is_quarantined_; }
void CloudServerConnection::SetQuarantine(bool value) {
  is_quarantined_ = value;
}

bool CloudServerConnection::Connect() {
  client_connection_.reset();
  client_connection_ = connection_factory_->CreateConnection(server().Load());
  return static_cast<bool>(client_connection_);
}

void CloudServerConnection::Disconnect() { client_connection_.reset(); }

ClientServerConnection* CloudServerConnection::client_connection() {
  if (client_connection_) {
    return client_connection_.get();
  }
  return nullptr;
}

Server::ptr const& CloudServerConnection::server() const {
  return cloud_server().server;
}

std::size_t CloudServerConnection::priority() const {
  return cloud_server().priority;
}

void CloudServerConnection::SetPriority(std::size_t priority) {
  assert(priority <= std::numeric_limits<std::uint16_t>::max() &&
         "cloud server priority exceeds persisted capacity");
  cloud_server().priority = static_cast<std::uint16_t>(priority);
}

CloudServer& CloudServerConnection::cloud_server() const {
  auto cloud = cloud_.Lock();
  assert(cloud && "cloud must outlive its connections");
  auto it = cloud->servers().find(server_id_);
  assert(it != cloud->servers().end() &&
         "cloud server connection must have a persistent server entry");
  return it->second;
}

}  // namespace ae
