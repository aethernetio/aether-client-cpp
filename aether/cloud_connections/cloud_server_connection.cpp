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

#include "aether/server.h"

namespace ae {

CloudServerConnection::CloudServerConnection(
    Ptr<Server> const& server, IServerConnectionFactory& connection_factory)
    : server_{server},
      connection_factory_{&connection_factory},
      priority_{},
      is_quarantined_{} {}

std::size_t CloudServerConnection::priority() const { return priority_; }
void CloudServerConnection::SetPriority(std::size_t priority) {
  priority_ = priority;
}

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
  client_connection_ = connection_factory_->CreateConnection(server());
  return static_cast<bool>(client_connection_);
}

void CloudServerConnection::Disconnect() { client_connection_.reset(); }

ClientServerConnection* CloudServerConnection::client_connection() {
  if (client_connection_) {
    return client_connection_.get();
  }
  return nullptr;
}

Ptr<Server> CloudServerConnection::server() const {
  auto server = server_.Lock();
  assert(server.get() != nullptr);
  return server;
}

}  // namespace ae
