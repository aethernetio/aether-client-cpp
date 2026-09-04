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
#include <optional>

#include "aether/config.h"

#include "aether-objects/obj/obj.h"
#include "aether-objects/ptr/ptr.h"
#include "aether/actions/action_pool.h"
#include "aether/cloud.h"
#include "aether/events/events.h"
#include "aether/executors/executors.h"
#include "aether/types/uid.h"

#include "aether/ae_actions/get_servers.h"
#include "aether/cloud_connections/cloud_subscription.h"

#include "aether/connection_manager/get_cloud_action.h"
#include "aether/connection_manager/get_cloud_aether.h"

namespace ae {
class Aether;
class Client;
class ClientCloudManager;
class CloudServerConnections;
struct ServerDescriptor;

namespace client_cloud_manager_internal {
struct CloudCache {
  AE_REFLECT_MEMBERS(subject_uid, version, version_confirmed, cloud);

  bool version_confirmed = false;
  Uid subject_uid;
  std::int64_t version = -1;
  Cloud::ptr cloud;

  // runtime info, do not serialize
  bool finalizing = false;
};

class GetCloudFromCache final : public GetCloudAction {
 public:
  GetCloudFromCache(AeContext const& ae_context, Cloud::ptr cloud);

  ResultEvent::Subscriber result_event() noexcept override;

 private:
  Cloud::ptr cloud_;
  ResultEvent result_event_;
};
}  // namespace client_cloud_manager_internal

class ClientCloudManager : public Obj {
  AE_OBJECT(ClientCloudManager, Obj, 0)

  ClientCloudManager() = default;

 public:
  static constexpr auto kGetCloudActionPoolCapacity =
      AE_CLOUD_GET_CLOUD_ACTION_POOL_CAPACITY;
  static constexpr auto kGetServersActionPoolCapacity =
      AE_CLOUD_GET_SERVERS_ACTION_POOL_CAPACITY;

  using CloudUpdateEvent =
      Event<void(Uid const& uid, Result<Cloud::ptr const&, int>)>;

  using GetCloudActionPool =
      ActionPool<AeContext,
                 std::variant<client_cloud_manager_internal::GetCloudFromCache,
                              GetCloudFromAether>,
                 kGetCloudActionPoolCapacity>;
  using GetServersPool =
      ActionPool<AeContext, GetServersAction, kGetServersActionPoolCapacity>;

  explicit ClientCloudManager(ObjProp prop, ObjPtr<Aether> aether,
                              ObjPtr<Client> client);

  AE_CLASS_NO_COPY_MOVE(ClientCloudManager)

  CloudUpdateEvent::Subscriber cloud_update_event();

  /**
   * \brief Make request for cloud for client_uid.
   * New request to client cloud is performed or cloud returned from cache.
   */
  GetCloudAction& GetCloud(Uid client_uid);

  /**
   * \brief Returns cloud from cache for client_uid or empty Cloud::ptr.
   */
  Cloud::ptr GetCachedCloud(Uid client_uid);

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, client_, cloud_cache_))

  void StartListenForCloudUpdate();

 private:
  void CloudConfigs(std::vector<CloudConfig> const& configs);
  void FinalizeCloudConfig(CloudConfig const& conf);
  auto MakeServersSender(std::vector<ServerId> const& sids);
  Cloud::ptr RegisterCloud(Uid uid, std::vector<Server::ptr> const& servers);

  GetCloudActionPool& get_cloud_action_pool();

  ObjPtr<Aether> aether_;
  ObjPtr<Client> client_;
  std::map<Uid, client_cloud_manager_internal::CloudCache> cloud_cache_;

  CloudUpdateEvent cloud_update_event_;
  CloudEventListener cloud_update_sub_;
  std::optional<GetCloudActionPool> cloud_actions_;
  std::optional<GetServersPool> get_servers_pool_;
  std::vector<std::unique_ptr<ex::AnyWaiter<
      ex::set_value_t(std::vector<Server::ptr>), ex::set_error_t(int)>>>
      make_servers_;
};
}  // namespace ae

#endif  // AETHER_CONNECTION_MANAGER_CLIENT_CLOUD_MANAGER_H_
