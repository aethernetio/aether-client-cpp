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

#ifndef AETHER_CONNECTION_MANAGER_SERVER_CONNECTION_SELECTOR_H_
#define AETHER_CONNECTION_MANAGER_SERVER_CONNECTION_SELECTOR_H_

#include <vector>

#include "aether/memory.h"
#include "aether/ptr/rc_ptr.h"
#include "aether/ptr/ptr_view.h"

#include "aether/connection_manager/iserver_priority_policy.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/server_connections/iserver_connection_factory.h"

namespace ae {
class Cloud;
class Server;

class ServerConnectionSelector {
 public:
  ServerConnectionSelector(ObjPtr<Cloud> const& cloud,
                           IServerPriorityPolicy& server_priority_policy,
                           std::unique_ptr<IServerConnectionFactory>
                               client_server_connection_factory);

  // Async for loop api
  void Init();
  void Next();
  // Return connection to current server
  RcPtr<ClientServerConnection> Get();
  bool End();

 private:
  std::vector<PtrView<Server>> servers_;
  std::vector<PtrView<Server>>::iterator current_server_;
  std::unique_ptr<IServerConnectionFactory> server_connection_factory_;
};
}  // namespace ae

#endif  // AETHER_CONNECTION_MANAGER_SERVER_CONNECTION_SELECTOR_H_
