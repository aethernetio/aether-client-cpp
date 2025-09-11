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

#include "aether/connection_manager/server_connection_manager.h"

#include "aether/client.h"
#include "aether/aether.h"
#include "aether/server.h"

namespace ae {
ServerConnectionManager::ServerConnectionFactory::ServerConnectionFactory(
    ServerConnectionManager& server_connection_manager)
    : server_connection_manager_{&server_connection_manager} {}

RcPtr<ClientServerConnection>
ServerConnectionManager::ServerConnectionFactory::CreateConnection(
    ObjPtr<Server> const& server) {
  return server_connection_manager_->CreateConnection(server);
}

ServerConnectionManager::ServerConnectionManager(ActionContext action_context,
                                                 ObjPtr<Aether> const& aether,
                                                 ObjPtr<Client> const& client)
    : action_context_{action_context},
      aether_{aether},
      client_{client},
      cached_connections_{} {}

std::unique_ptr<IServerConnectionFactory>
ServerConnectionManager::GetServerConnectionFactory() {
  return std::make_unique<ServerConnectionFactory>(*this);
}

RcPtr<ClientServerConnection> ServerConnectionManager::CreateConnection(
    Server::ptr const& server) {
  auto in_cache = FindInCache(server->server_id);
  if (in_cache) {
    return in_cache;
  }

  // make new connection
  auto aether = aether_.Lock();
  assert(aether);
  auto client = client_.Lock();
  assert(client);

  auto connection = MakeRcPtr<ClientServerConnection>(action_context_, aether,
                                                      client, server);
  // save in cache
  cached_connections_[server->server_id] = connection;
  return connection;
}

RcPtr<ClientServerConnection> ServerConnectionManager::FindInCache(
    ServerId server_id) const {
  auto it = cached_connections_.find(server_id);
  if (it != cached_connections_.end()) {
    return it->second.lock();
  }
  return nullptr;
}

}  // namespace ae
