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

#include "aether/server_connections/server_connection.h"

#include "aether/server.h"

namespace ae {
ServerConnection::ServerConnection(ObjPtr<Server> const& server,
                                   IServerConnectionFactory& connection_factory)
    : server_{server},
      connection_factory_{&connection_factory},
      priority_{},
      is_connection_{},
      is_quarantined_{} {}

std::size_t ServerConnection::priority() const { return priority_; }

bool ServerConnection::quarantine() const { return is_quarantined_; }
void ServerConnection::quarantine(bool value) { is_quarantined_ = value; }

void ServerConnection::Restream() {
  if (client_connection_) {
    client_connection_->Restream();
  }
}

void ServerConnection::BeginConnection(std::size_t priority) {
  priority_ = priority;
  if (!is_connection_) {
    Server::ptr server = server_.Lock();
    assert(server);
    client_connection_.Reset();
    client_connection_ = connection_factory_->CreateConnection(server);
  }
  is_connection_ = true;
}

void ServerConnection::EndConnection(std::size_t priority) {
  priority_ = priority;
  is_connection_ = false;
}

ClientServerConnection* ServerConnection::ClientConnection() {
  if (client_connection_) {
    return client_connection_.get();
  }
  return nullptr;
}

ObjPtr<Server> ServerConnection::server() const {
  Server::ptr server = server_.Lock();
  assert(server);
  return server;
}

}  // namespace ae
