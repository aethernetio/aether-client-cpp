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

#ifdef AE_DISTILLATION

Aether::Aether(Domain* domain) : Obj{domain} {
  auto self_ptr = MakePtrFromThis(this);

  client_prefab = domain->CreateObj<Client>(GlobalId::kClientFactory, self_ptr);
  client_prefab.SetFlags(ae::ObjFlags::kUnloadedByDefault);

  tele_statistics =
      domain->CreateObj<tele::TeleStatistics>(GlobalId::kTeleStatistics);

  AE_TELE_DEBUG(AetherCreated);
}
#endif  // AE_DISTILLATION

Aether::~Aether() { AE_TELE_DEBUG(AetherDestroyed); }

ActionPtr<SelectClientAction> Aether::SelectClient(
    [[maybe_unused]] Uid parent_uid, std::uint32_t client_id) {
  AE_TELED_DEBUG("Select parent {}'s client with id {}", parent_uid, client_id);

  auto client = FindClient(client_id);
  if (client) {
    return ActionPtr<SelectClientAction>{*action_processor, client};
  }
#if AE_SUPPORT_REGISTRATION
  auto registration = RegisterClient(parent_uid, client_id);
  return ActionPtr<SelectClientAction>{*action_processor, *registration};
#else
  return ActionPtr<SelectClientAction>{*action_processor};
#endif
}

void Aether::AddServer(Server::ptr&& s) {
  servers_.insert({s->server_id, std::move(s)});
}

Server::ptr Aether::GetServer(ServerId server_id) {
  auto it = servers_.find(server_id);
  if (it == std::end(servers_)) {
    return {};
  }
  if (!it->second) {
    domain_->LoadRoot(it->second);
  }
  return it->second;
}

void Aether::Update(TimePoint current_time) {
  update_time_ = action_processor->Update(current_time);
}

Client::ptr Aether::FindClient(std::uint32_t client_id) {
  auto client_it = clients_.find(client_id);
  if (client_it == std::end(clients_)) {
    return {};
  }
  if (!client_it->second) {
    domain_->LoadRoot(client_it->second);
  }
  return client_it->second;
}

#if AE_SUPPORT_REGISTRATION
ActionPtr<Registration> Aether::RegisterClient(Uid parent_uid,
                                               std::uint32_t client_id) {
  // try to find existent registrations
  auto reg_it = registration_actions_.find(client_id);
  if (reg_it != std::end(registration_actions_)) {
    return ActionPtr{reg_it->second};
  }

  if (!registration_cloud) {
    domain_->LoadRoot(registration_cloud);
  }
  auto self_ptr = MakePtrFromThis(this);

  auto new_client = domain_->LoadCopy(client_prefab);
  new_client.SetFlags(ObjFlags::kUnloadedByDefault);

  // registration new client is long termed process
  // after registration done, add it to clients list
  // user also can get new client after
  auto [new_reg_it, _] = registration_actions_.emplace(
      client_id,
      ActionPtr<Registration>(*action_processor, self_ptr, registration_cloud,
                              parent_uid, std::move(new_client)));

  // on registration success save client to client_ and remove registration
  // action at the end
  registration_subscriptions_.Push(
      new_reg_it->second->StatusEvent().Subscribe(
          OnResult{[this, client_id](auto const& action) {
            auto client = action.client();
            assert(client);
            clients_.emplace(client_id, std::move(client));
          }}),
      new_reg_it->second->FinishedEvent().Subscribe(
          [this, client_id]() { registration_actions_.erase(client_id); }));

  return ActionPtr{new_reg_it->second};
}
#endif
}  // namespace ae
