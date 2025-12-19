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

#include "aether/common.h"
#include "aether/memory.h"
#include "aether/obj/obj.h"
#include "aether/obj/dummy_obj.h"  // IWYU pragma: keep
#include "aether/actions/action_ptr.h"
#include "aether/actions/action_context.h"
#include "aether/actions/action_processor.h"
#include "aether/ae_actions/select_client.h"
#include "aether/events/multi_subscription.h"
#include "aether/tele/traps/tele_statistics.h"
#include "aether/registration/registration.h"  // IWYU pragma: keep

#include "aether/cloud.h"
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

 public:
  explicit Aether(Domain* domain);
  ~Aether() override;

  // User-facing API.
  operator ActionContext() const { return ActionContext{*action_processor}; }

  ActionPtr<SelectClientAction> SelectClient(Uid parent_uid,
                                             std::string client_id);

  void AddServer(std::shared_ptr<Server> s);
  std::shared_ptr<Server> GetServer(ServerId server_id);

  std::unique_ptr<ActionProcessor> action_processor =
      make_unique<ActionProcessor>();

  std::shared_ptr<Cloud> registration_cloud;

  std::shared_ptr<Crypto> crypto;
  std::shared_ptr<IPoller> poller;
  std::shared_ptr<DnsResolver> dns_resolver;

  std::shared_ptr<AdapterRegistry> adapter_registry;

  std::shared_ptr<tele::TeleStatistics> tele_statistics;

 private:
  std::shared_ptr<Client> FindClient(std::string const& client_id);
  std::shared_ptr<Client> AddClient(Uid parent_uid, std::string client_id);
#if AE_SUPPORT_REGISTRATION
  ActionPtr<Registration> FindRegistration(std::string const& client_id);
  ActionPtr<Registration> RegisterClient(std::shared_ptr<Client> client);
#endif

  std::vector<std::shared_ptr<Client>> clients_;
  std::map<ServerId, std::shared_ptr<Server>> servers_;

#if AE_SUPPORT_REGISTRATION
  std::map<std::string, ActionPtr<Registration>> registration_actions_;
  MultiSubscription registration_subscriptions_;
#endif
};

}  // namespace ae

#endif  // AETHER_AETHER_H_
