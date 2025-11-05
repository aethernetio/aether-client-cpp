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

#include "aether/adapters/ethernet.h"

#include <utility>

#include "aether/aether.h"
#include "aether/adapters/adapter_tele.h"

namespace ae {

#ifdef AE_DISTILLATION
EthernetAdapter::EthernetAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
                                 DnsResolver::ptr dns_resolver, Domain* domain)
    : Adapter{domain},
      ethernet_access_point_{domain->CreateObj<EthernetAccessPoint>(
          std::move(aether), std::move(poller), std::move(dns_resolver))} {
  AE_TELED_INFO("EthernetAdapter created");
}
#endif  // AE_DISTILLATION

std::vector<AccessPoint::ptr> EthernetAdapter::access_points() {
  return {ethernet_access_point_};
}

}  // namespace ae
