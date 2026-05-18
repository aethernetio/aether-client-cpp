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

#include <map>
#include <array>
#include <optional>

#include "aether/obj/obj.h"
#include "aether/ptr/ptr.h"
#include "aether/actions/action.h"
#include "aether/actions/action_pool.h"

#include "aether/cloud.h"
#include "aether/types/uid.h"
#include "aether/types/result.h"
#include "aether/events/events.h"

#include "aether/ae_actions/get_servers.h"
#include "aether/ae_actions/get_client_cloud.h"

namespace ae {
class Aether;
class Client;
class ClientCloudManager;
class CloudServerConnections;
struct ServerDescriptor;

/**
 * \brief Action to get a cloud.
 */
class GetCloudAction : public Action {
 public:
  using ResultEvent = Event<void(Result<Cloud::ptr, int>)>;

  virtual ResultEvent::Subscriber result_event() noexcept = 0;
};

namespace client_cloud_manager_internal {
class GetCloudFromCache final : public GetCloudAction {
 public:
  GetCloudFromCache(AeContext const& ae_context, Cloud::ptr cloud);

  ResultEvent::Subscriber result_event() noexcept override;

 private:
  Cloud::ptr cloud_;
  ResultEvent result_event_;
};

class GetCloudFromAether final : public GetCloudAction {
 public:
  explicit GetCloudFromAether(AeContext const& ae_context, Aether& aether,
                              ClientCloudManager& client_cloud_manager,
                              CloudServerConnections& cloud_connection,
                              Uid client_uid);

  ResultEvent::Subscriber result_event() noexcept override;

 private:
  void RequestCloud();
  void BuildServers(std::vector<ServerId> cloud);
  std::vector<Server::ptr> BuildNewServers(
      std::vector<ServerDescriptor> const& descriptors);
  void RegisterCloud(std::vector<Server::ptr> servers);
  void Failed();

  AeContext ae_context_;
  Aether* aether_;
  ClientCloudManager* client_cloud_manager_;
  CloudServerConnections* cloud_connection_;
  Uid client_uid_;
  ResultEvent result_event_;
  std::optional<GetClientCloudAction> get_client_cloud_action_;
  Subscription get_client_cloud_sub_;
  std::optional<GetServersAction> get_servers_action_;
  Subscription get_servers_sub_;
  std::vector<ServerId> cloud_sids_;
  std::vector<Server::ptr> servers_;
};

}  // namespace client_cloud_manager_internal

class ClientCloudManager : public Obj {
  AE_OBJECT(ClientCloudManager, Obj, 0)

  ClientCloudManager() = default;

 public:
  explicit ClientCloudManager(ObjProp prop, ObjPtr<Aether> aether,
                              ObjPtr<Client> client);

  AE_CLASS_NO_COPY_MOVE(ClientCloudManager)

  GetCloudAction& GetCloud(Uid client_uid);

  Cloud::ptr RegisterCloud(Uid uid, std::vector<Server::ptr> servers);

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, client_, cloud_cache_))

 private:
  Obj::ptr aether_;
  Obj::ptr client_;
  std::map<Uid, Cloud::ptr> cloud_cache_;
  std::optional<ActionPool<
      AeContext,
      std::variant<client_cloud_manager_internal::GetCloudFromCache,
                   client_cloud_manager_internal::GetCloudFromAether>,
      5>>
      cloud_actions_;
};
}  // namespace ae

#endif  // AETHER_CONNECTION_MANAGER_CLIENT_CLOUD_MANAGER_H_
