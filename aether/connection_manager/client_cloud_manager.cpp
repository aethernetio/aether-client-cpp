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

#include "aether/aether.h"
#include "aether/client.h"
#include "aether/work_cloud.h"

#include "aether/types/state_machine.h"
#include "aether/ae_actions/get_servers.h"
#include "aether/ae_actions/get_client_cloud.h"
#include "aether/work_cloud_api/server_descriptor.h"
#include "aether/client_connections/cloud_connection.h"

#include "aether/client_connections/client_connections_tele.h"

namespace ae {
namespace client_cloud_manager_internal {
class GetCloudFromCache final : public GetCloudAction {
 public:
  GetCloudFromCache(ActionContext action_context, Cloud::ptr cloud)
      : GetCloudAction{action_context}, cloud_{std::move(cloud)} {}

  UpdateStatus Update() override { return UpdateStatus::Result(); }
  Cloud::ptr cloud() override { return std::move(cloud_); }
  void Stop() override {};

 private:
  Cloud::ptr cloud_;
};

class GetCloudFromAether : public GetCloudAction {
 public:
  enum class State : std::uint8_t {
    kGetCloud,
    kDone,
    kError,
    kStopped,
  };

  explicit GetCloudFromAether(ActionContext action_context, Aether& aether,
                              ClientCloudManager& client_cloud_manager,
                              CloudConnection& cloud_connection, Uid client_uid)
      : GetCloudAction{action_context},
        action_context_{action_context},
        aether_{&aether},
        client_cloud_manager_{&client_cloud_manager},
        cloud_connection_{&cloud_connection},
        client_uid_{client_uid},
        state_{State::kGetCloud} {
    state_.changed_event().Subscribe([this](auto&&) { Action::Trigger(); });
  }

  UpdateStatus Update() override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kGetCloud:
          RequestCloud();
          break;
        case State::kDone:
          return UpdateStatus::Result();
        case State::kError:
          return UpdateStatus::Error();
        case State::kStopped:
          return UpdateStatus::Stop();
      }
    }
    return {};
  }

  Cloud::ptr cloud() override { return std::move(cloud_); }
  void Stop() override { state_ = State::kStopped; };

 private:
  void RequestCloud() {
    get_client_cloud_action_ = OwnActionPtr<GetClientCloudAction>(
        action_context_, client_uid_, *cloud_connection_,
        RequestPolicy::Replica{cloud_connection_->count_connections()});

    get_client_cloud_sub_ = get_client_cloud_action_->StatusEvent().Subscribe(
        ActionHandler{OnResult{[this](auto const& action) {
                        BuildServers(action.cloud());
                        state_ = State::kDone;
                      }},
                      OnError{[this]() { state_ = State::kError; }}});
  }

  void BuildServers(std::vector<ServerId> cloud) {
    cloud_sids_ = std::move(cloud);
    // find servers in cache and make request servers for missing
    std::vector<ServerId> missing_servers;
    for (auto const& sid : cloud_sids_) {
      auto server = aether_->GetServer(sid);
      if (server) {
        servers_.emplace_back(std::move(server));
      } else {
        missing_servers.push_back(sid);
      }
    }
    if (!missing_servers.empty()) {
      // If there are missing servers, resolve them from the cloud
      get_servers_action_ = OwnActionPtr<GetServersAction>{
          action_context_, missing_servers, *cloud_connection_,
          RequestPolicy::Replica{cloud_connection_->count_connections()}};
      get_servers_sub_ =
          get_servers_action_->StatusEvent().Subscribe(ActionHandler{
              OnResult{[this](auto const& action) mutable {
                auto new_servers = BuildNewServers(action.servers());
                // extend the existing servers with the new ones
                servers_.insert(std::end(servers_), std::begin(new_servers),
                                std::end(new_servers));
                // sort servers by cloud order
                std::sort(std::begin(servers_), std::end(servers_),
                          [&](auto const& left, auto const& right) {
                            auto left_order = std::find(std::begin(cloud_sids_),
                                                        std::end(cloud_sids_),
                                                        left->server_id);
                            auto right_order = std::find(
                                std::begin(cloud_sids_), std::end(cloud_sids_),
                                right->server_id);
                            return left_order < right_order;
                          });

                RegisterCloud(std::move(servers_));
                state_ = State::kDone;
              }},
              OnError{[this]() { state_ = State::kError; }},
          });
    } else {
      // all servers already known to aether, just build a new cloud
      RegisterCloud(std::move(servers_));
      state_ = State::kDone;
    }
  }

  std::vector<Server::ptr> BuildNewServers(
      std::vector<ServerDescriptor> const& descriptors) {
    std::vector<Server::ptr> servers;
    for (auto const& sd : descriptors) {
      std::vector<Endpoint> endpoints;
      for (auto const& ip : sd.ips) {
        for (auto const& pp : ip.protocol_and_ports) {
          endpoints.emplace_back(Endpoint{{ip.ip, pp.port}, pp.protocol});
        }
      }
      Server::ptr s = aether_->domain_->CreateObj<Server>(
          sd.server_id, endpoints, aether_->adapter_registry);
      aether_->StoreServer(s);
      servers.emplace_back(std::move(s));
    }
    return servers;
  }

  void RegisterCloud(std::vector<Server::ptr> servers) {
    cloud_ =
        client_cloud_manager_->RegisterCloud(client_uid_, std::move(servers));
  }

  ActionContext action_context_;
  Aether* aether_;
  ClientCloudManager* client_cloud_manager_;
  CloudConnection* cloud_connection_;
  Uid client_uid_;
  StateMachine<State> state_;
  OwnActionPtr<GetClientCloudAction> get_client_cloud_action_;
  Subscription get_client_cloud_sub_;
  OwnActionPtr<GetServersAction> get_servers_action_;
  Subscription get_servers_sub_;
  std::vector<ServerId> cloud_sids_;
  std::vector<Server::ptr> servers_;
  Cloud::ptr cloud_;
};

}  // namespace client_cloud_manager_internal

ClientCloudManager::ClientCloudManager(ObjPtr<Aether> aether,
                                       ObjPtr<Client> client, Domain* domain)
    : Obj(domain), aether_{std::move(aether)}, client_{std::move(client)} {}

ActionPtr<GetCloudAction> ClientCloudManager::GetCloud(Uid client_uid) {
  if (!aether_) {
    domain_->LoadRoot(aether_);
  }
  assert(aether_);
  auto* aether = aether_.as<Aether>();

  auto cached = cloud_cache_.find(client_uid);
  if (cached != cloud_cache_.end()) {
    if (!cached->second) {
      domain_->LoadRoot(cached->second);
    }
    assert(cached->second);
    return ActionPtr<client_cloud_manager_internal::GetCloudFromCache>{
        *aether, cached->second};
  }
  // get from aethernet
  if (!client_) {
    domain_->LoadRoot(client_);
  }
  assert(client_);

  auto* client = client_.as<Client>();
  return ActionPtr<client_cloud_manager_internal::GetCloudFromAether>{
      *aether, *aether, *this, client->cloud_connection(), client_uid};
}

Cloud::ptr ClientCloudManager::RegisterCloud(Uid uid,
                                             std::vector<Server::ptr> servers) {
  if (!aether_) {
    domain_->LoadRoot(aether_);
  }
  auto new_cloud = domain_->CreateObj<WorkCloud>(uid);
  assert(new_cloud);
  new_cloud->SetServers(std::move(servers));

  cloud_cache_[uid] = new_cloud;
  return new_cloud;
}

}  // namespace ae
