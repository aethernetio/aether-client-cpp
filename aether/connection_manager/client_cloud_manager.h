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

#ifndef AETHER_CONNECTION_MANAGER_CLIENT_CLOUD_MANAGER_H_
#define AETHER_CONNECTION_MANAGER_CLIENT_CLOUD_MANAGER_H_

#include <vector>

#include "aether/obj/obj.h"
#include "aether/ptr/ptr.h"
#include "aether/actions/action_ptr.h"

#include "aether/cloud.h"

#include "aether/ae_actions/get_client_cloud_connection.h"

#include "aether/connection_manager/cloud_cache.h"
#include "aether/client_connections/client_connection.h"
#include "aether/connection_manager/server_connection_selector.h"
#include "aether/connection_manager/client_server_connection_pool.h"

namespace ae {
class Aether;
class Client;

namespace ccm_internal {
class CachedServerConnectionFactory;
}

class ClientCloudManager : public Obj {
  friend class ccm_internal::CachedServerConnectionFactory;

  AE_OBJECT(ClientCloudManager, Obj, 0)

  ClientCloudManager() = default;

 public:
  explicit ClientCloudManager(ObjPtr<Aether> aether, ObjPtr<Client> client,
                              Domain* domain);

  AE_CLASS_NO_COPY_MOVE(ClientCloudManager)

  Ptr<ClientConnection> GetClientConnection();
  ActionPtr<GetClientCloudConnection> GetClientConnection(Uid client_uid);

  Ptr<ClientConnection> CreateClientConnection(Cloud::ptr const& cloud);

  Cloud::ptr RegisterCloud(
      Uid uid, std::vector<ServerDescriptor> const& server_descriptors);

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, client_, cloud_cache_,
                             client_server_connection_pool_))

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_);
    dnv(aether_, client_, cloud_cache_);
  }
  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_);
    dnv(aether_, client_, cloud_cache_);
  }

 private:
  void RegisterCloud(Uid uid, Cloud::ptr cloud);

  Obj::ptr aether_;
  Obj::ptr client_;

  CloudCache cloud_cache_;
  ClientServerConnectionPool client_server_connection_pool_;
};
}  // namespace ae

#endif  // AETHER_CONNECTION_MANAGER_CLIENT_CLOUD_MANAGER_H_
