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

#ifndef AETHER_SERVER_CONNECTIONS_SERVER_CONNECTION_H_
#define AETHER_SERVER_CONNECTIONS_SERVER_CONNECTION_H_

#include "aether/ptr/rc_ptr.h"
#include "aether/obj/obj_ptr.h"
#include "aether/ptr/ptr_view.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/server_connections/iserver_connection_factory.h"

namespace ae {
class Server;
/**
 * \brief Represent the connection to the server.
 * It's not the actual transport but the all information about the server.
 */
class ServerConnection {
 public:
  ServerConnection(ObjPtr<Server> const& server,
                   IServerConnectionFactory& connection_factory);

  /**
   * \brief The stream to write data for that server.
   * If there is no connection object, create it.
   */
  ClientServerConnection& ClientConnection();

  ObjPtr<Server> server() const;

 private:
  PtrView<Server> server_;
  IServerConnectionFactory* connection_factory_;
  RcPtr<ClientServerConnection> client_connection_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_SERVER_CONNECTION_H_
