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

#ifndef AETHER_CONNECTION_MANAGER_SERVER_CONNECTION_MANAGER_H_
#define AETHER_CONNECTION_MANAGER_SERVER_CONNECTION_MANAGER_H_

#include <map>

#include "aether/ptr/rc_ptr.h"
#include "aether/obj/obj_ptr.h"
#include "aether/ptr/ptr_view.h"
#include "aether/actions/action_context.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/server_connections/iserver_connection_factory.h"

namespace ae {
class Aether;
class Client;

class ServerConnectionManager {
  class ServerConnectionFactory final : public IServerConnectionFactory {
   public:
    explicit ServerConnectionFactory(
        ServerConnectionManager& server_connection_manager);

    RcPtr<ClientServerConnection> CreateConnection(
        ObjPtr<Server> const& server) override;

   private:
    ServerConnectionManager* server_connection_manager_;
  };

 public:
  ServerConnectionManager(ActionContext action_context,
                          ObjPtr<Aether> const& aether,
                          ObjPtr<Client> const& client);

  std::unique_ptr<IServerConnectionFactory> GetServerConnectionFactory();

  RcPtr<ClientServerConnection> CreateConnection(ObjPtr<Server> const& server);

  RcPtr<ClientServerConnection> FindInCache(ServerId server_id) const;

 private:
  ActionContext action_context_;
  PtrView<Aether> aether_;
  PtrView<Client> client_;
  std::map<ServerId, RcPtrView<ClientServerConnection>> cached_connections_;
};
}  // namespace ae

#endif  // AETHER_CONNECTION_MANAGER_SERVER_CONNECTION_MANAGER_H_
