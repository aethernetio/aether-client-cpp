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

#include "aether/work_cloud.h"

#include "aether/aether_tele.h"

namespace ae {

Aether::Aether(Domain* domain) : Obj{domain} {
  tele_statistics = std::make_shared<tele::TeleStatistics>(domain_);
  AE_TELE_DEBUG(AetherCreated);
}

Aether::~Aether() { AE_TELE_DEBUG(AetherDestroyed); }

ActionPtr<SelectClientAction> Aether::SelectClient(Uid parent_uid,
                                                   std::string client_id) {
  AE_TELED_DEBUG("Select client parent_uid={}, id={}", parent_uid, client_id);

  auto client = FindClient(client_id);
  if (!client) {
    client = AddClient(parent_uid, std::move(client_id));
  }

  // if existing client
  if (!client->uid().empty()) {
    return ActionPtr<SelectClientAction>{*action_processor, std::move(client)};
  }

  // register new client
#if AE_SUPPORT_REGISTRATION
  auto reg = FindRegistration(client->client_id());
  if (!reg) {
    reg = RegisterClient(std::move(client));
  }
  return ActionPtr<SelectClientAction>{*action_processor, *reg};
#else
  return ActionPtr<SelectClientAction>{*action_processor};
#endif
}

void Aether::AddServer(std::shared_ptr<Server> s) {
  servers_.insert({s->server_id, std::move(s)});
}

std::shared_ptr<Server> Aether::GetServer(ServerId server_id) {
  auto it = servers_.find(server_id);
  if (it == std::end(servers_)) {
    return {};
  }
  return it->second;
}

std::shared_ptr<Client> Aether::FindClient(std::string const& client_id) {
  auto client_it = std::find_if(
      std::begin(clients_), std::end(clients_),
      [&](auto const& client) { return client->client_id() == client_id; });
  if (client_it == std::end(clients_)) {
    return {};
  }
  return *client_it;
}

std::shared_ptr<Client> Aether::AddClient(Uid parent_uid,
                                          std::string client_id) {
  auto client = std::make_shared<Client>(*this, parent_uid,
                                         std::move(client_id), domain_);
  clients_.push_back(client);
  return client;
}

#if AE_SUPPORT_REGISTRATION
ActionPtr<Registration> Aether::FindRegistration(std::string const& client_id) {
  auto reg_it = registration_actions_.find(client_id);
  if (reg_it != std::end(registration_actions_)) {
    return reg_it->second;
  }
  return {};
}

ActionPtr<Registration> Aether::RegisterClient(std::shared_ptr<Client> client) {
  auto client_id = client->client_id();
  auto [new_reg_it, _] = registration_actions_.emplace(
      client_id,
      ActionPtr<Registration>(*action_processor, *this, *registration_cloud,
                              std::move(client)));

  // on registration success  remove registration action at the end
  registration_subscriptions_.Push(
      new_reg_it->second->FinishedEvent().Subscribe(
          [this, client_id]() { registration_actions_.erase(client_id); }));

  return ActionPtr{new_reg_it->second};
}
#endif
}  // namespace ae
