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

#include "aether/connection_manager/client_cloud_manager.h"

#include <utility>
#include <cassert>
#include <optional>

#include "aether/aether.h"
#include "aether/client.h"
#include "aether/work_cloud.h"

#include "aether/work_cloud_api/server_descriptor.h"
#include "aether/cloud_connections/cloud_server_connections.h"

namespace ae {
namespace client_cloud_manager_internal {
GetCloudFromCache::GetCloudFromCache(AeContext const& ae_context,
                                     Cloud::ptr cloud)
    : cloud_{std::move(cloud)} {
  ae_context.scheduler().Task([this]() {
    result_event_.Emit(Ok{std::move(cloud_)});
    Finish();
  });
}

GetCloudFromCache::ResultEvent::Subscriber
GetCloudFromCache::result_event() noexcept {
  return EventSubscriber{result_event_};
}

GetCloudFromAether::GetCloudFromAether(AeContext const& ae_context,
                                       Aether& aether,
                                       ClientCloudManager& client_cloud_manager,
                                       CloudServerConnections& cloud_connection,
                                       Uid const& client_uid)
    : ae_context_{ae_context},
      aether_{&aether},
      client_cloud_manager_{&client_cloud_manager},
      cloud_connection_{&cloud_connection},
      client_uid_{client_uid} {
  RequestCloud();
}

GetCloudFromAether::ResultEvent::Subscriber
GetCloudFromAether::result_event() noexcept {
  return EventSubscriber{result_event_};
}

void GetCloudFromAether::RequestCloud() {
  get_client_cloud_action_.emplace(
      ae_context_, client_uid_, *cloud_connection_,
      RequestPolicy::Replica{cloud_connection_->count_connections()});

  get_client_cloud_sub_ = get_client_cloud_action_->result_event().Subscribe(
      [this](auto const& res) {
        if (res) {
          auto const& resolved = res.value();
          if (!resolved.empty()) {
            BuildServers(resolved);
          } else {
            Failed();
          }
        } else {
          Failed();
        }
      });
}

void GetCloudFromAether::BuildServers(std::vector<ServerId> cloud) {
  cloud_sids_ = std::move(cloud);
  // find servers in cache and make request servers for missing
  std::vector<ServerId> missing_servers;
  for (auto const& sid : cloud_sids_) {
    auto server = aether_->GetServer(sid);
    if (server.is_valid()) {
      servers_.emplace_back(std::move(server));
    } else {
      missing_servers.push_back(sid);
    }
  }
  if (!missing_servers.empty()) {
    // If there are missing servers, resolve them from the cloud
    get_servers_action_.emplace(
        ae_context_, missing_servers, *cloud_connection_,
        RequestPolicy::Replica{cloud_connection_->count_connections()});
    get_servers_sub_ = get_servers_action_->result_event().Subscribe(
        [this](auto const& result) mutable {
          if (result) {
            auto new_servers = BuildNewServers(result.value());
            // extend the existing servers with the new ones
            servers_.insert(std::end(servers_), std::begin(new_servers),
                            std::end(new_servers));
            // sort servers by cloud order
            std::sort(std::begin(servers_), std::end(servers_),
                      [&](auto const& left, auto const& right) {
                        auto left_order = std::find(std::begin(cloud_sids_),
                                                    std::end(cloud_sids_),
                                                    left.Load()->server_id);
                        auto right_order = std::find(std::begin(cloud_sids_),
                                                     std::end(cloud_sids_),
                                                     right.Load()->server_id);
                        return left_order < right_order;
                      });

            RegisterCloud(std::move(servers_));
          } else {
            Failed();
          }
        });
  } else {
    // all servers already known to aether, just build a new cloud
    RegisterCloud(std::move(servers_));
  }
}

std::vector<Server::ptr> GetCloudFromAether::BuildNewServers(
    std::vector<ServerDescriptor> const& descriptors) {
  std::vector<Server::ptr> servers;
  for (auto const& sd : descriptors) {
    std::vector<Endpoint> endpoints;
    for (auto const& ip : sd.ips) {
      for (auto const& pp : ip.protocol_and_ports) {
        endpoints.emplace_back(Endpoint{{ip.ip, pp.port}, pp.protocol});
      }
    }
    auto s = Server::ptr::Create(aether_->domain, sd.server_id, endpoints,
                                 aether_->adapter_registry);
    aether_->StoreServer(s);
    servers.emplace_back(std::move(s));
  }
  return servers;
}

void GetCloudFromAether::RegisterCloud(std::vector<Server::ptr> servers) {
  auto cloud =
      client_cloud_manager_->RegisterCloud(client_uid_, std::move(servers));
  result_event_.Emit(Ok{std::move(cloud)});
  Finish();
}

void GetCloudFromAether::Failed() {
  result_event_.Emit(Error{1});
  Finish();
}
}  // namespace client_cloud_manager_internal

ClientCloudManager::ClientCloudManager(ObjProp prop, ObjPtr<Aether> aether,
                                       ObjPtr<Client> client)
    : Obj{prop}, aether_{std::move(aether)}, client_{std::move(client)} {}

GetCloudAction& ClientCloudManager::GetCloud(Uid client_uid) {
  auto aether = Aether::ptr{aether_}.Load();
  assert(aether);

  if (!cloud_actions_) {
    cloud_actions_.emplace(*aether);
  }

  auto cached = cloud_cache_.find(client_uid);
  if (cached != cloud_cache_.end()) {
    assert(cached->second.is_valid());
    auto* action =
        cloud_actions_
            ->Create<client_cloud_manager_internal::GetCloudFromCache>(
                *aether, cached->second);
    assert(action != nullptr && "Failed to create GetCloudFromCache action");
    return *action;
  }
  // get from aethernet

  auto client = Client::ptr{client_}.Load();
  assert(client);

  auto* action =
      cloud_actions_->Create<client_cloud_manager_internal::GetCloudFromAether>(
          *aether, *aether, *this, client->cloud_connection(), client_uid);
  assert(action != nullptr && "Failed to create GetCloudFromAether action");
  return *action;
}

Cloud::ptr ClientCloudManager::RegisterCloud(Uid uid,
                                             std::vector<Server::ptr> servers) {
  auto new_cloud = WorkCloud::ptr::Create(domain, uid);
  new_cloud->SetServers(std::move(servers));

  cloud_cache_[uid] = new_cloud;
  return new_cloud;
}

}  // namespace ae
