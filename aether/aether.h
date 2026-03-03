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

#include "aether/clock.h"
#include "aether/memory.h"
#include "aether/obj/obj.h"
#include "aether/actions/action_ptr.h"
#include "aether/types/client_config.h"

#include "aether/actions/action_context.h"
#include "aether/ae_actions/select_client.h"

#include "aether/uap/uap.h"

namespace ae {
class Server;
class Client;
class Crypto;
class IPoller;
class DnsResolver;
class Registration;
class TimeSyncAction;
class AdapterRegistry;
class ActionProcessor;
class RegistrationCloud;
class RegistrationCloud;

namespace tele {
class TeleStatistics;
}

class Aether : public Obj {
  AE_OBJECT(Aether, Obj, 0)

  Aether();

 public:
  // Internal.
  explicit Aether(ObjProp prop);

  ~Aether() override;

  AE_OBJECT_REFLECT(AE_MMBRS(client_prefab, registration_cloud, crypto,
                             clients_, servers_, tele_statistics, poller,
                             dns_resolver, adapter_registry, uap,
                             select_client_actions_))

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_);
    dnv(client_prefab, registration_cloud, crypto, clients_, servers_,
        tele_statistics, poller, dns_resolver, adapter_registry, uap);
  }

  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_);
    dnv(client_prefab, registration_cloud, crypto, clients_, servers_,
        tele_statistics, poller, dns_resolver, adapter_registry, uap);
  }

  void Update(TimePoint current_time) override;

  // User-facing API.
  operator ActionContext() const;

  ObjPtr<Client> CreateClient(ClientConfig const& config,
                              std::string const& client_id);
  ActionPtr<SelectClientAction> SelectClient(Uid parent_uid,
                                             std::string const& client_id);

  void StoreServer(ObjPtr<Server> s);
  ObjPtr<Server> GetServer(ServerId server_id);

  Obj::ptr client_prefab;
  Obj::ptr registration_cloud;

  Obj::ptr crypto;
  Obj::ptr poller;
  Obj::ptr dns_resolver;

  Obj::ptr adapter_registry;
  Uap::ptr uap;

  Obj::ptr tele_statistics;

  std::unique_ptr<ActionProcessor> action_processor;

 private:
  ObjPtr<Client> FindClient(std::string const& client_id);
  void StoreClient(ObjPtr<Client> client);

  ActionPtr<SelectClientAction> FindSelectClientAction(
      std::string const& client_id);
  ActionPtr<SelectClientAction> MakeSelectClient() const;
  ActionPtr<SelectClientAction> MakeSelectClient(
      ObjPtr<Client> const& client) const;
#if AE_SUPPORT_REGISTRATION
  ActionPtr<SelectClientAction> MakeSelectClient(
      ActionPtr<Registration> registration, std::string const& client_id);

 public:
  ActionPtr<Registration> RegisterClient(Uid parent_uid);

 private:
#endif

  void MakeTimeSyncAction(ObjPtr<Client> const& client);

  std::map<std::string, Obj::ptr> clients_;
  std::map<ServerId, Obj::ptr> servers_;

  std::map<std::string, ActionPtr<SelectClientAction>> select_client_actions_;
#if AE_TIME_SYNC_ENABLED
  ActionPtr<TimeSyncAction> time_sync_action_;
#endif
};

}  // namespace ae

#endif  // AETHER_AETHER_H_
