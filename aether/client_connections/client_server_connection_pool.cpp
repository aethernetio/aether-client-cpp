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

#include "aether/client_connections/client_server_connection_pool.h"

namespace ae {

RcPtr<ClientServerConnection> ClientServerConnectionPool::Find(
    ServerId server_id, ObjId channel_obj_id) {
  auto key = Key{server_id, channel_obj_id};
  auto it = connections_.find(key);
  if (it == std::end(connections_)) {
    return {};
  }
  auto connection = it->second.lock();
  if (!connection) {
    connections_.erase(it);
    return {};
  }
  return connection;
}

void ClientServerConnectionPool::Add(
    ServerId server_id, ObjId channel_obj_id,
    RcPtr<ClientServerConnection> const& connection) {
  auto key = Key{server_id, channel_obj_id};
  connections_[key] = connection;
}
}  // namespace ae
