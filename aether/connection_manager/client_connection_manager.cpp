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

#include "aether/connection_manager/client_connection_manager.h"

#include <algorithm>

#include "aether/cloud.h"

namespace ae {
ClientConnectionManager::ClientConnectionManager(
    ObjPtr<Cloud> const& cloud,
    std::unique_ptr<IServerConnectionFactory>&& connection_factory)
    : cloud_{cloud}, connection_factory_{std::move(connection_factory)} {
  InitServerConnections();
}

std::vector<ServerConnection>& ClientConnectionManager::server_connections() {
  return server_connections_;
}

void ClientConnectionManager::InitServerConnections() {
  Cloud::ptr cloud = cloud_.Lock();
  assert(cloud);
  server_connections_.reserve(cloud->servers().size());
  for (auto& server : cloud->servers()) {
    if (!server) {
      cloud->LoadServer(server);
    }
    assert(server);
    server_connections_.emplace_back(server, *connection_factory_);
  }
  std::sort(std::begin(server_connections_), std::end(server_connections_),
            [](auto const& lhs, auto const& rhs) {
              return lhs.server()->server_id < rhs.server()->server_id;
            });
}

}  // namespace ae
