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

#ifndef AETHER_CONNECTION_MANAGER_CLIENT_CONNECTION_MANAGER_H_
#define AETHER_CONNECTION_MANAGER_CLIENT_CONNECTION_MANAGER_H_

#include "aether/obj/obj_ptr.h"
#include "aether/ptr/ptr_view.h"

#include "aether/server_connections/server_connection.h"
#include "aether/server_connections/iserver_connection_factory.h"

namespace ae {
class Cloud;

/**
 * \brief Manager of all connections to the client's cloud
 */
class ClientConnectionManager {
 public:
  ClientConnectionManager(
      ObjPtr<Cloud> const& cloud,
      std::unique_ptr<IServerConnectionFactory>&& connection_factory);

  std::vector<ServerConnection>& server_connections();

 private:
  void InitServerConnections();

  PtrView<Cloud> cloud_;
  std::unique_ptr<IServerConnectionFactory> connection_factory_;
  std::vector<ServerConnection> server_connections_;
};
}  // namespace ae

#endif  // AETHER_CONNECTION_MANAGER_CLIENT_CONNECTION_MANAGER_H_
