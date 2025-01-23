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

#ifndef AETHER_CLIENT_CONNECTIONS_CLIENT_CONNECTION_MANAGER_H_
#define AETHER_CLIENT_CONNECTIONS_CLIENT_CONNECTION_MANAGER_H_

#include <vector>

#include "aether/obj/obj.h"
#include "aether/obj/ptr.h"
#include "aether/actions/action_view.h"
#include "aether/actions/action_list.h"

#include "aether/cloud.h"

#include "aether/ae_actions/get_client_cloud_connection.h"
#include "aether/client_connections/client_connection.h"
#include "aether/client_connections/server_connection_selector.h"
#include "aether/client_connections/cloud_cache.h"
#include "aether/client_connections/client_server_connection_pull.h"

namespace ae {
class Aether;
class Client;

class ClientConnectionManager : public Obj {
  AE_OBJECT(ClientConnectionManager, Obj, 0)

  friend class CachedServerConnectionFactory;

 public:
  explicit ClientConnectionManager(ObjPtr<Aether> aether, ObjPtr<Client> client,
                                   Domain* domain);

  AE_CLASS_NO_COPY_MOVE(ClientConnectionManager)

  Ptr<ClientConnection> GetClientConnection();
  ActionView<GetClientCloudConnection> GetClientConnection(Uid client_uid);

  void RegisterCloud(Uid uid,
                     std::vector<ServerDescriptor> const& server_descriptors);

  Ptr<ServerConnectionSelector> GetCloudServerConnectionSelector(Uid uid);

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
    dnv(aether_, client_, cloud_cache_);
  }

 private:
  void RegisterCloud(Uid uid, Cloud::ptr cloud);

  Obj::ptr aether_;
  Obj::ptr client_;

  CloudCache cloud_cache_;
  ClientServerConnectionPull client_server_connection_pull_;

  Ptr<ActionList<GetClientCloudConnection>> get_client_cloud_connections_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_CLIENT_CONNECTION_MANAGER_H_
