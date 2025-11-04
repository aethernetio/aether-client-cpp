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
    kCloudResolve,
    kWaitCloudResolve,
    kDone,
    kError,
    kStopped,
  };

  explicit GetCloudFromAether(ActionContext action_context,
                              ClientCloudManager& client_cloud_manager,
                              CloudConnection& cloud_connection, Uid client_uid)
      : GetCloudAction{action_context},
        action_context_{action_context},
        client_cloud_manager_{&client_cloud_manager},
        cloud_connection_{&cloud_connection},
        client_uid_{client_uid},
        state_{State::kCloudResolve} {
    state_.changed_event().Subscribe([this](auto&&) { Action::Trigger(); });
  }

  UpdateStatus Update() override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kCloudResolve:
          ResolveCloud();
          break;
        case State::kWaitCloudResolve:
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
  void ResolveCloud() {
    get_client_cloud_action_ = ActionPtr<GetClientCloudAction>(
        action_context_, client_uid_, *cloud_connection_,
        RequestPolicy::Replica{cloud_connection_->count_connections()});

    get_client_cloud_sub_ = get_client_cloud_action_->StatusEvent().Subscribe(
        ActionHandler{OnResult{[this](auto const& action) {
                        RegisterCloud(action.server_descriptors());
                        state_ = State::kDone;
                      }},
                      OnError{[this]() { state_ = State::kError; }}});
    state_ = State::kWaitCloudResolve;
  }

  void RegisterCloud(std::map<ServerId, ServerDescriptor> const& servers) {
    cloud_ = client_cloud_manager_->RegisterCloud(client_uid_, servers);
  }

  ActionContext action_context_;
  ClientCloudManager* client_cloud_manager_;
  CloudConnection* cloud_connection_;
  Uid client_uid_;
  StateMachine<State> state_;
  ActionPtr<GetClientCloudAction> get_client_cloud_action_;
  Subscription get_client_cloud_sub_;
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
      *aether, *this, client->cloud_connection(), client_uid};
}

Cloud::ptr ClientCloudManager::RegisterCloud(
    Uid uid, std::map<ServerId, ServerDescriptor> const& server_descriptors) {
  if (!aether_) {
    domain_->LoadRoot(aether_);
  }
  assert(aether_);
  auto* aether = aether_.as<Aether>();
  auto new_cloud = domain_->CreateObj<WorkCloud>();
  assert(new_cloud);

  for (auto const& [_, descriptor] : server_descriptors) {
    auto server_id = descriptor.server_id;
    auto cached_server = aether->GetServer(server_id);
    if (cached_server) {
      new_cloud->AddServer(cached_server);
      continue;
    }

    std::vector<UnifiedAddress> endpoints;
    for (auto const& ip : descriptor.ips) {
      for (auto const& pp : ip.protocol_and_ports) {
        endpoints.emplace_back(
            IpAddressPortProtocol{{ip.ip, pp.port}, pp.protocol});
      }
    }

    auto server = domain_->CreateObj<Server>(server_id, std::move(endpoints));
    assert(server);
    server->Register(aether->adapter_registry);

    new_cloud->AddServer(server);
    aether->AddServer(std::move(server));
  }

  cloud_cache_[uid] = new_cloud;
  return new_cloud;
}

}  // namespace ae
