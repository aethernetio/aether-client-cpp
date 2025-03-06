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

#ifndef AETHER_CLIENT_CONNECTIONS_CLIENT_SERVER_CONNECTION_POOL_H_
#define AETHER_CLIENT_CONNECTIONS_CLIENT_SERVER_CONNECTION_POOL_H_

#include <map>

#include "aether/common.h"
#include "aether/obj/obj_id.h"
#include "aether/ptr/rc_ptr.h"
#include "aether/client_connections/client_server_connection.h"

namespace ae {
/**
 * \brief Cache storage for client's connection to servers.
 */
class ClientServerConnectionPool {
  struct Key {
    friend bool operator<(Key const& left, Key const& right) {
      if (left.server_id == right.server_id) {
        return left.channel_obj_id.id() < right.channel_obj_id.id();
      }
      return left.server_id < right.server_id;
    }

    ServerId server_id;
    ObjId channel_obj_id;
  };

 public:
  ClientServerConnectionPool() = default;
  AE_CLASS_NO_COPY_MOVE(ClientServerConnectionPool);

  RcPtr<ClientServerConnection> Find(ServerId server_id, ObjId channel_obj_id);
  void Add(ServerId server_id, ObjId channel_obj_id,
           RcPtr<ClientServerConnection> const& connection);

 private:
  std::map<Key, RcPtrView<ClientServerConnection>> connections_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_CLIENT_SERVER_CONNECTION_POOL_H_
