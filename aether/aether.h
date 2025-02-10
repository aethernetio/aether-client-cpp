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

#include <vector>
#include <map>

#include "aether/common.h"
#include "aether/obj/obj.h"
#include "aether/obj/dummy_obj.h"
#include "aether/client.h"
#include "aether/cloud.h"
#include "aether/registration_cloud.h"
#include "aether/crypto.h"
#include "aether/events/multi_subscription.h"
#include "aether/tele/traps/tele_statistics.h"
#include "aether/actions/action_list.h"
#include "aether/actions/action_processor.h"
#include "aether/poller/poller.h"
#include "aether/dns/dns_resolve.h"

#include "aether/ae_actions/registration/registration.h"

namespace ae {
class Registration;

class Aether : public Obj {
  AE_OBJECT(Aether, Obj, 0)

  Aether() = default;

 public:
  // Internal.
#ifdef AE_DISTILLATION
  explicit Aether(Domain* domain);
#endif  // AE_DISTILLATION

  ~Aether() override;

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
    dnv(client_prefab, cloud_prefab, registration_cloud, crypto, clients_,
        servers_, tele_statistics_, poller, dns_resolver, adapter_factories);
  }

  void Update(TimePoint current_time) override;

  // User-facing API.
#if AE_SUPPORT_REGISTRATION
  ActionView<Registration> RegisterClient(Uid parent_uid);
#endif

  void AddServer(Server::ptr&& s);
  Server::ptr GetServer(ServerId server_id);
  std::vector<Client::ptr>& clients();
  tele::TeleStatistics::ptr const& tele_statistics() const;

  Ptr<ActionProcessor> action_processor = MakePtr<ActionProcessor>();

  Cloud::ptr cloud_prefab;
#if AE_SUPPORT_REGISTRATION
  RegistrationCloud::ptr registration_cloud;
#else
  DummyObj::ptr registration_cloud;
#endif

  Crypto::ptr crypto;
  IPoller::ptr poller;
#if AE_SUPPORT_CLOUD_DNS
  DnsResolver::ptr dns_resolver;
#else
  DummyObj::ptr dns_resolver;
#endif

  std::vector<Adapter::ptr> adapter_factories;

 private:
  Client::ptr client_prefab;

  std::vector<Client::ptr> clients_;
  std::map<ServerId, Server::ptr> servers_;

  tele::TeleStatistics::ptr tele_statistics_;

#if AE_SUPPORT_REGISTRATION
  Ptr<ActionList<Registration>> registration_actions_;
  MultiSubscription registration_subscriptions_;
#endif
};

}  // namespace ae

#endif  // AETHER_AETHER_H_
