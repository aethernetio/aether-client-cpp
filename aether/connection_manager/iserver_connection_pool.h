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

#ifndef AETHER_CONNECTION_MANAGER_ISERVER_CONNECTION_POOL_H_
#define AETHER_CONNECTION_MANAGER_ISERVER_CONNECTION_POOL_H_

#include "aether/ptr/rc_ptr.h"
#include "aether/reflect/reflect.h"
#include "aether/server_connections/client_server_connection.h"

namespace ae {
/**
 * \brief Pool of server connections.
 * TopConnection is one may be considered as default connection.
 * Rotate changes the default server.
 */
class IServerConnectionPool {
 public:
  virtual ~IServerConnectionPool() = default;

  /**
   * \brief Get the top connection from the pool
   * \return Client server connection, may be nullptr if pool is empty.
   */
  virtual RcPtr<ClientServerConnection> TopConnection() = 0;
  /**
   * \brief Rotate the connections in pool
   * \return true if rotation was successful, false otherwise.
   */
  virtual bool Rotate() = 0;

  AE_REFLECT()
};
}  // namespace ae

#endif  // AETHER_CONNECTION_MANAGER_ISERVER_CONNECTION_POOL_H_
