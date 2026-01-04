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

#ifndef AETHER_AETHER_H_
#define AETHER_AETHER_H_

#include <map>
#include <string>

#include "aether/common.h"
#include "aether/memory.h"
#include "aether/obj/obj.h"
#include "aether/actions/action_ptr.h"
#include "aether/types/client_config.h"
#include "aether/actions/action_context.h"
#include "aether/actions/action_processor.h"
#include "aether/ae_actions/select_client.h"
#include "aether/tele/traps/tele_statistics.h"
#include "aether/registration/registration.h"  // IWYU pragma: keep

#include "aether/client.h"
#include "aether/crypto.h"
#include "aether/server.h"
#include "aether/poller/poller.h"
#include "aether/dns/dns_resolve.h"
#include "aether/adapter_registry.h"
#include "aether/registration_cloud.h"

namespace ae {
class Aether : public Obj {
  AE_OBJECT(Aether, Obj, 0)

  Aether() = default;

 public:
  // Internal.
#ifdef AE_DISTILLATION
  explicit Aether(Domain* domain);
#endif  // AE_DISTILLATION

  ~Aether() override;

  AE_OBJECT_REFLECT(AE_MMBRS(client_prefab, registration_cloud, crypto,
                             clients_, servers_, tele_statistics, poller,
                             dns_resolver, adapter_registry,
                             select_client_actions_))

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_);
    dnv(client_prefab, registration_cloud, crypto, clients_, servers_,
        tele_statistics, poller, dns_resolver, adapter_registry);
  }

  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_);
    dnv(client_prefab, registration_cloud, crypto, clients_, servers_,
        tele_statistics, poller, dns_resolver, adapter_registry);
  }

  void Update(TimePoint current_time) override;

  // User-facing API.
  operator ActionContext() const { return ActionContext{*action_processor}; }

  Client::ptr CreateClient(ClientConfig const& config,
                           std::string const& client_id);
  ActionPtr<SelectClientAction> SelectClient(Uid parent_uid,
                                             std::string const& client_id);

  void StoreServer(Server::ptr s);
  Server::ptr GetServer(ServerId server_id);

  std::unique_ptr<ActionProcessor> action_processor =
      make_unique<ActionProcessor>();

  RegistrationCloud::ptr registration_cloud;

  Crypto::ptr crypto;
  IPoller::ptr poller;
  DnsResolver::ptr dns_resolver;

  AdapterRegistry::ptr adapter_registry;

  tele::TeleStatistics::ptr tele_statistics;

 private:
  Client::ptr FindClient(std::string const& client_id);
  void StoreClient(Client::ptr client);

  ActionPtr<SelectClientAction> FindSelectClientAction(
      std::string const& client_id);
  ActionPtr<SelectClientAction> MakeSelectClient() const;
  ActionPtr<SelectClientAction> MakeSelectClient(
      Client::ptr const& client) const;
#if AE_SUPPORT_REGISTRATION
  ActionPtr<SelectClientAction> MakeSelectClient(
      ActionPtr<Registration> registration, std::string const& client_id);

 public:
  ActionPtr<Registration> RegisterClient(Uid parent_uid);

 private:
#endif

  Client::ptr client_prefab;

  std::map<std::string, Client::ptr> clients_;
  std::map<ServerId, Server::ptr> servers_;

  std::map<std::string, ActionPtr<SelectClientAction>> select_client_actions_;
};

}  // namespace ae

#endif  // AETHER_AETHER_H_
