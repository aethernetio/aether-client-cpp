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

#include <type_traits>

#include "aether/cloud.h"
#include "aether/server_list/server_list.h"
#include "aether/client_connections/client_server_connection.h"
#include "aether/client_connections/iserver_connection_factory.h"

namespace ae {
// TODO: add an iterator interface maybe ?
class ServerConnectionSelector {
 public:
  ServerConnectionSelector(
      Ptr<Cloud> cloud,
      Ptr<IServerConnectionFactory> client_server_connection_factory);

  void Init();
  void Next();
  // Return stream to current server
  Ptr<ClientServerConnection> GetConnection();
  bool End();

 private:
  Ptr<ServerList> server_list_;
  Ptr<IServerConnectionFactory> server_connection_factory_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_SERVER_CONNECTION_SELECTOR_H_
