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

#include "aether/actions/action_context.h"

#include "aether/obj/obj_ptr.h"
#include "aether/global_ids.h"
#include "aether/client.h"
#include "aether/crypto.h"
#include "aether/server.h"

#include "aether/registration_cloud.h"
#include "aether/work_cloud.h"

#include "aether/poller/epoll_poller.h"
#include "aether/poller/kqueue_poller.h"
#include "aether/poller/freertos_poller.h"
#include "aether/poller/win_poller.h"

#include "aether/dns/dns_c_ares.h"
#include "aether/dns/esp32_dns_resolve.h"

#include "aether/aether_tele.h"

namespace ae {

#ifdef AE_DISTILLATION

Aether::Aether(Domain* domain) : Obj{domain} {
  auto self_ptr = MakePtrFromThis(this);

  client_prefab = domain->CreateObj<Client>(GlobalId::kClientFactory, self_ptr);
  client_prefab.SetFlags(ae::ObjFlags::kUnloadedByDefault);

  crypto = domain->CreateObj<Crypto>(GlobalId::kCrypto);

  cloud_prefab = domain->CreateObj<WorkCloud>(GlobalId::kCloudFactory);
  cloud_prefab.SetFlags(ae::ObjFlags::kUnloadedByDefault);

  tele_statistics_ =
      domain->CreateObj<tele::TeleStatistics>(GlobalId::kTeleStatistics);

#  if defined EPOLL_POLLER_ENABLED
  poller = domain->CreateObj<EpollPoller>(GlobalId::kPoller);
#  elif defined KQUEUE_POLLER_ENABLED
  poller = domain->CreateObj<KqueuePoller>(GlobalId::kPoller);
#  elif defined FREERTOS_POLLER_ENABLED
  poller = domain->CreateObj<FreertosPoller>(GlobalId::kPoller);
#  elif defined WIN_POLLER_ENABLED
  poller = domain->CreateObj<WinPoller>(GlobalId::kPoller);
#  endif

#  if defined DNS_RESOLVE_ARES_ENABLED
  dns_resolver =
      domain->CreateObj<DnsResolverCares>(GlobalId::kDnsResolver, self_ptr);
  dns_resolver.SetFlags(ObjFlags::kUnloadedByDefault);
#  elif defined ESP32_DNS_RESOLVER_ENABLED
  dns_resolver =
      domain->CreateObj<Esp32DnsResolver>(GlobalId::kDnsResolver, self_ptr);
  dns_resolver.SetFlags(ObjFlags::kUnloadedByDefault);
#  endif
  AE_TELE_DEBUG(AetherCreated);
}
#endif  // AE_DISTILLATION

Aether::~Aether() { AE_TELE_DEBUG(AetherDestroyed); }

#if AE_SUPPORT_REGISTRATION
ActionView<Registration> Aether::RegisterClient(Uid parent_uid) {
  if (!registration_cloud) {
    domain_->LoadRoot(registration_cloud);
  }

  auto new_client = domain_->LoadCopy(client_prefab);
  new_client.SetFlags(ObjFlags::kUnloadedByDefault);

  auto self_ptr = MakePtrFromThis(this);

  if (!registration_actions_) {
    registration_actions_.emplace(ActionContext{*action_processor});
  }

  // registration new client is long termed process
  // after registration done, add it to clients list
  // user also can get new client after
  auto registration_action = registration_actions_->Emplace(
      std::move(self_ptr), parent_uid, std::move(new_client));

  registration_subscriptions_.Push(
      registration_action
          ->SubscribeOnResult([this](auto const& action) {
            auto client = action.client();
            assert(client);
            clients_.push_back(std::move(client));
          })
          .Once());

  return registration_action;
}
#endif

std::vector<Client::ptr>& Aether::clients() { return clients_; }

tele::TeleStatistics::ptr const& Aether::tele_statistics() const {
  return tele_statistics_;
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

}  // namespace ae
