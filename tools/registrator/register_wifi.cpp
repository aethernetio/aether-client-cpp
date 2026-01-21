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

#include "registrator/register_wifi.h"

#include "aether/aether.h"

namespace ae::reg {

#ifdef AE_DISTILLATION
RegisterWifiAdapter::RegisterWifiAdapter(ObjPtr<Aether> aether,
                                         IPoller::ptr poller,
                                         DnsResolver::ptr dns_resolver,
                                         wifi_init, Domain* domain)
    : ParentWifiAdapter{std::move(aether),       std::move(poller),
                        std::move(dns_resolver), std::move(wifi_init),
                        domain},
      ethernet_adapter_{domain->CreateObj<EthernetAdapter>(aether_, poller_,
                                                           dns_resolver_)} {}
#endif  // AE_DISTILLATION

std::vector<AccessPoint::ptr> RegisterWifiAdapter::access_points() {
  return ethernet_adapter_->access_points();
}

RegisterWifiAdapter::NewAccessPoint::Subscriber
RegisterWifiAdapter::new_access_point() {
  return ethernet_adapter_->new_access_point();
}

}  // namespace ae::reg
