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

#include <cassert>
#include <optional>
#include <utility>

#include "aether/aether.h"
#include "aether/client.h"
#include "aether/work_cloud.h"

#include "aether/tele.h"

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

void BuildNewServers(Aether::ptr const& aether,
                     std::vector<Server::ptr>& servers,
                     std::vector<ServerDescriptor> const& server_descriptors) {
  auto const& a = aether.Load();
  assert(a && "Aether did not loaded");

  for (auto const& sd : server_descriptors) {
    // check if server exits first
    if (auto s = a->GetServer(sd.server_id); s.is_valid()) {
      servers.emplace_back(std::move(s));
      continue;
    }

    std::vector<Endpoint> endpoints;
    for (auto const& ip : sd.ips) {
      for (auto const& pp : ip.protocol_and_ports) {
        endpoints.emplace_back(Endpoint{{ip.ip, pp.port}, pp.protocol});
      }
    }

    AE_TELED_DEBUG("Make new server id:{}, endpoints:{}", sd.server_id,
                   endpoints);

    auto s = Server::ptr::Create(aether.domain(), sd.server_id, endpoints,
                                 a->adapter_registry);
    aether->StoreServer(s);
    servers.emplace_back(std::move(s));
  }
}

auto SplitMissingLoaded(Aether::ptr const& aether,
                        std::vector<ServerId> const& sids) {
  return ex::create<ex::set_value_t(std::vector<Server::ptr>,
                                    std::vector<ServerId>)>(
      [aether_ = aether, sids](auto& ctx) noexcept {
        // get servers from aether cache and make missing server list
        auto const& a = aether_.Load();
        assert(a && "Aether did not loaded");

        std::vector<Server::ptr> servers;
        servers.reserve(sids.size());
        std::vector<ServerId> missing_servers;
        missing_servers.reserve(sids.size());

        for (auto const& sid : sids) {
          auto server = a->GetServer(sid);
          if (server.is_valid()) {
            servers.emplace_back(std::move(server));
          } else {
            missing_servers.emplace_back(sid);
          }
        }
        ex::set_value(std::move(ctx.receiver), std::move(servers),
                      std::move(missing_servers));
      });
}

auto LoadMissing(Aether::ptr const& aether, Client::ptr const& client,
                 ClientCloudManager::GetServersPool& get_servers_pool) {
  return ex::let_value([aether, client, &get_servers_pool](
                           auto& servers, auto& missing) noexcept {
    return ex::create<ex::set_value_t(std::vector<Server::ptr>),
                      ex::set_error_t(int)>([&](auto& ctx) noexcept {
      // request server descriptors for missing server ids
      if (missing.empty()) {
        return ex::set_value(std::move(ctx.receiver), std::move(servers));
      }
      auto const& a = aether.Load();
      auto const& c = client.Load();
      assert(a && c && "Aether and client did not loaded");

      auto* get_servers = get_servers_pool.Create(
          *a, missing, c->cloud_connection(), RequestPolicy::All{});
      assert(get_servers != nullptr && "Get servers action did not created");
      get_servers->result_event().Subscribe([&](auto const& res) {
        if (res) {
          BuildNewServers(aether, servers, res.value());
          ex::set_value(std::move(ctx.receiver), std::move(servers));
        } else {
          ex::set_error(std::move(ctx.receiver), 1);
        }
      });
    });
  });
}

auto SortNewServers(std::vector<ServerId> const& sids) {
  return ex::then([sids](std::vector<Server::ptr>&& servers) noexcept {
    // servers should be sorted in initial sids order
    std::sort(std::begin(servers), std::end(servers),
              [&](auto const& left, auto const& right) {
                auto left_order = std::find(std::begin(sids), std::end(sids),
                                            left.Load()->server_id);
                auto right_order = std::find(std::begin(sids), std::end(sids),
                                             right.Load()->server_id);
                return left_order < right_order;
              });
    return std::move(servers);
  });
}

}  // namespace client_cloud_manager_internal

ClientCloudManager::ClientCloudManager(ObjProp prop, ObjPtr<Aether> aether,
                                       ObjPtr<Client> client)
    : Obj{prop}, aether_{std::move(aether)}, client_{std::move(client)} {
  // save cloud cache for current client
  Client::ptr{client}.WithLoaded([&](auto const& c) {
    cloud_cache_.emplace(c->uid(), client_cloud_manager_internal::CloudCache{
                                       .version_confirmed = true,
                                       .subject_uid = c->uid(),
                                       .version = 0,
                                       .cloud = c->cloud(),
                                   });
  });

  // init the rest
  Init();
}

ClientCloudManager::CloudUpdateEvent::Subscriber
ClientCloudManager::cloud_update_event() {
  return EventSubscriber{cloud_update_event_};
}

GetCloudAction& ClientCloudManager::GetCloud(Uid client_uid) {
  AE_TELED_DEBUG("Ask cloud for uid: {}", client_uid);

  auto aether = Aether::ptr{aether_}.Load();
  assert(aether && "Aether did not loaded");

  assert(cloud_actions_ && "Cloud actions did not initiated");

  auto cached = cloud_cache_.find(client_uid);
  if ((cached != cloud_cache_.end()) && cached->second.cloud.is_valid()) {
    // cloud stored in cache, return GetCloudFromCache
    auto* action =
        cloud_actions_
            ->Create<client_cloud_manager_internal::GetCloudFromCache>(
                *aether, cached->second.cloud);
    assert(action != nullptr && "Failed to create GetCloudFromCache action");
    return *action;
  }

  // get from aethernet
  auto client = Client::ptr{client_}.Load();
  assert(client);

  auto* action = cloud_actions_->Create<GetCloudFromAether>(
      *aether, *this, client->cloud_connection(), client_uid);
  assert(action != nullptr && "Failed to create GetCloudFromAether action");
  return *action;
}

void ClientCloudManager::Init() {
  auto aether = Aether::ptr{aether_}.Load();
  assert(aether && "Aether must be loaded");

  cloud_actions_.emplace(*aether);
  get_servers_pool_.emplace(*aether);

  ListenForCloudUpdate();
}

void ClientCloudManager::ListenForCloudUpdate() {
  auto client = Client::ptr{client_}.Load();
  assert(client != nullptr && "Client does not loaded");

  cloud_update_sub_ = CloudEventListener{
      ApiEventSubscriber{[this](ClientApiSafe& client_api,
                                CloudServerConnection* /*server_connection*/) {
        return client_api.send_cloud_configs().Subscribe(
            MethodPtr<&ClientCloudManager::CloudConfigs>{this});
      }},
      client->cloud_connection(), RequestPolicy::All{}};
}

auto ClientCloudManager::MakeServersSender(std::vector<ServerId> const& sids) {
  using namespace client_cloud_manager_internal;  // NOLINT(*using-namespace)

  auto aether = Aether::ptr{aether_};
  auto client = Client::ptr{client_};
  assert(get_servers_pool_.has_value() && "Get servers pool did not initiated");

  return SplitMissingLoaded(aether, sids) |
         LoadMissing(aether, client, *get_servers_pool_) | SortNewServers(sids);
}

void ClientCloudManager::CloudConfigs(std::vector<CloudConfig> const& configs) {
  for (auto const& conf : configs) {
    AE_TELED_DEBUG("Got cloud config update subject:{}, ver:{}, cloud:[{}]",
                   conf.subject_uid, conf.config_version, conf.cloud.sids);

    // find this config in cache
    auto it = cloud_cache_.find(conf.subject_uid);
    // new cloud config or new cloud config version and finalizing of config
    // is not in progress
    if (it == cloud_cache_.end()) {
      // new config
      cloud_cache_.emplace(conf.subject_uid,
                           client_cloud_manager_internal::CloudCache{
                               .version_confirmed = false,
                               .subject_uid = conf.subject_uid,
                               .version = conf.config_version,
                               .cloud = {},  // leave cloud empty
                               .finalizing = true,
                           });

      FinalizeCloudConfig(conf);
    } else if (!it->second.finalizing &&
               (it->second.version < conf.config_version)) {
      it->second.version_confirmed = false,
      it->second.subject_uid = conf.subject_uid;
      it->second.version = conf.config_version;
      it->second.finalizing = true;

      FinalizeCloudConfig(conf);
    } else if (it->second.finalizing) {
      // duplicate config
      return;
    } else {
      // just confirm version
      it->second.version_confirmed = true;
    }
  }
}

void ClientCloudManager::FinalizeCloudConfig(CloudConfig const& conf) {
  auto aether = Aether::ptr{aether_}.Load();

  AE_TELED_DEBUG("Finalize servers for new cloud config [{}]", conf.cloud.sids);
  // make async waiter for building the new cloud
  make_servers_.emplace_back(
      std::make_unique<ex::AnyWaiter<ex::set_value_t(std::vector<Server::ptr>),
                                     ex::set_error_t(int)>>(
          AeContext{*aether}, MakeServersSender(conf.cloud.sids),
          [this, conf](std::optional<Result<std::vector<Server::ptr>, int>>
                           res) noexcept {
            assert(res.has_value() && "The result must exists");
            if (res->IsOk()) {
              auto cloud =
                  RegisterCloud(conf.subject_uid, std::move(*res).value());
              cloud_update_event_.Emit(conf.subject_uid,
                                       Ok<Cloud::ptr const&>{cloud});
            } else {
              AE_TELED_ERROR("Cloud resolve failed!, Error {}", res->error());
              // TODO: how to handle such error?
              cloud_update_event_.Emit(conf.subject_uid, Error{res->error()});
            }
          }));
}

Cloud::ptr ClientCloudManager::RegisterCloud(Uid uid,
                                             std::vector<Server::ptr> servers) {
  auto it = cloud_cache_.find(uid);
  assert((it != cloud_cache_.end()) &&
         "Cloud should be in cache before register");
  // update existing cloud in cache
  it->second.finalizing = false;
  if (!it->second.cloud.is_valid()) {
    it->second.cloud = WorkCloud::ptr::Create(domain, uid);
  }
  it->second.cloud.Load()->SetServers(std::move(servers));
  return it->second.cloud;
}

}  // namespace ae
