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

#ifndef AETHER_CLIENT_CONNECTIONS_SERVER_CONNECTION_SELECTOR_H_
#define AETHER_CLIENT_CONNECTIONS_SERVER_CONNECTION_SELECTOR_H_

#include "aether/memory.h"
#include "aether/ptr/rc_ptr.h"
#include "aether/server_list/server_list.h"
#include "aether/client_connections/client_server_connection.h"
#include "aether/client_connections/iserver_connection_factory.h"

namespace ae {
class Cloud;

class ServerConnectionSelector {
 public:
  ServerConnectionSelector(
      ObjPtr<Cloud> const& cloud,
      std::unique_ptr<ServerListPolicy>&& server_list_policy,
      std::unique_ptr<IServerConnectionFactory>&&
          client_server_connection_factory);

  void Init();
  void Next();
  // Return connection to current server
  RcPtr<ClientServerConnection> GetConnection();
  bool End();

 private:
  ServerList server_list_;
  std::unique_ptr<IServerConnectionFactory> server_connection_factory_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_SERVER_CONNECTION_SELECTOR_H_
