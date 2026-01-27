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

#include "aether/aether.h"

#include <utility>

#include "aether/global_ids.h"
#include "aether/obj/obj_ptr.h"

#include "aether/work_cloud.h"

#include "aether/aether_tele.h"

namespace ae {

Aether::Aether(ObjProp prop) : Obj{prop} { AE_TELE_DEBUG(AetherCreated); }

Aether::~Aether() { AE_TELE_DEBUG(AetherDestroyed); }

void Aether::Update(TimePoint current_time) {
  update_time = action_processor->Update(current_time);
}

Client::ptr Aether::CreateClient(ClientConfig const& config,
                                 std::string const& client_id) {
  auto client = FindClient(client_id);
  if (client.is_valid()) {
    return client;
  }
  // create new client
  AE_TELED_DEBUG("Create new client {} with uid {}", client_id, config.uid);

  client = client_prefab.Clone();
  client.SetFlags(ObjFlags::kUnloadedByDefault);

  // make servers
  std::vector<Server::ptr> servers;
  for (auto const& server_config : config.cloud) {
    if (auto cached = GetServer(server_config.id); cached) {
      servers.emplace_back(std::move(cached));
    } else {
      auto new_server = Server::ptr::Create(
          domain, server_config.id, server_config.endpoints, adapter_registry);
      StoreServer(new_server);
      servers.emplace_back(std::move(new_server));
    }
  }

  auto client_cloud = WorkCloud::ptr::Create(domain, config.uid);
  [[maybe_unused]] auto res =  // ~(^.^)~
      client_cloud.WithLoaded([&](auto const& cloud) {
        cloud->SetServers(std::move(servers));
      }) &&  // ~(^.^)~
      client.WithLoaded([&](auto const& client) {
        client->SetConfig(client_id, config.parent_uid, config.uid,
                          config.ephemeral_uid, config.master_key,
                          std::move(client_cloud));
      });
  assert(res && "Failed to set client config");

  StoreClient(client);
  return client;
}

ActionPtr<SelectClientAction> Aether::SelectClient(
    [[maybe_unused]] Uid parent_uid, std::string const& client_id) {
  AE_TELED_DEBUG("Select client {} with parent uid {}", client_id, parent_uid);

  auto select_action = FindSelectClientAction(client_id);
  if (select_action) {
    return select_action;
  }

  auto client = FindClient(client_id);
  if (client.is_valid()) {
    return MakeSelectClient(client);
  }
// register new client
#if AE_SUPPORT_REGISTRATION
  auto registration = RegisterClient(parent_uid);
  return MakeSelectClient(std::move(registration), client_id);
#else
  return MakeSelectClient();
#endif
}

void Aether::StoreServer(Server::ptr s) {
  servers_.insert({s->server_id, std::move(s)});
}

Server::ptr Aether::GetServer(ServerId server_id) {
  auto it = servers_.find(server_id);
  if (it == std::end(servers_)) {
    return {};
  }
  return it->second;
}

Client::ptr Aether::FindClient(std::string const& client_id) {
  auto client_it = clients_.find(client_id);
  if (client_it == std::end(clients_)) {
    return {};
  }
  return client_it->second;
}

void Aether::StoreClient(Client::ptr client) {
  assert(client.is_valid() && "Client is invalid");
  clients_[client.Load()->id()] = std::move(client);
}

ActionPtr<SelectClientAction> Aether::FindSelectClientAction(
    std::string const& client_id) {
  auto select_act_it = select_client_actions_.find(client_id);
  if (select_act_it != std::end(select_client_actions_)) {
    return ActionPtr{select_act_it->second};
  }
  return {};
}

ActionPtr<SelectClientAction> Aether::MakeSelectClient() const {
  return ActionPtr<SelectClientAction>{*action_processor};
}

ActionPtr<SelectClientAction> Aether::MakeSelectClient(
    Client::ptr const& client) const {
  return ActionPtr<SelectClientAction>{*action_processor, client};
}

#if AE_SUPPORT_REGISTRATION
ActionPtr<SelectClientAction> Aether::MakeSelectClient(
    ActionPtr<Registration> registration, std::string const& client_id) {
  auto select_action = ActionPtr<SelectClientAction>{
      *action_processor, *this, std::move(registration), client_id};
  select_client_actions_[client_id] = select_action;
  return select_action;
}

ActionPtr<Registration> Aether::RegisterClient(Uid parent_uid) {
  auto reg_cloud = registration_cloud.Load();
  assert(reg_cloud && "Registration cloud not loaded");

  // registration new client is long termed process
  // after registration done, add it to clients list
  // user also can get new client after
  return ActionPtr<Registration>(*action_processor, *this, reg_cloud,
                                 parent_uid);
}
#endif
}  // namespace ae
