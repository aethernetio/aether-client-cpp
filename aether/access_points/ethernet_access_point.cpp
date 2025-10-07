
/*
 * Copyright 2025 Aethernet Inc.
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

#include "aether/access_points/ethernet_access_point.h"

#include <utility>

#include "aether/aether.h"
#include "aether/poller/poller.h"
#include "aether/dns/dns_resolve.h"
#include "aether/channels/ethernet_channel.h"

namespace ae {
EthernetAccessPoint::EthernetAccessPoint(ObjPtr<Aether> aether,
                                         ObjPtr<IPoller> poller,
                                         ObjPtr<DnsResolver> dns_resolver,
                                         Domain* domain)
    : AccessPoint{domain},
      aether_{std::move(aether)},
      poller_{std::move(poller)},
      dns_resolver_{std::move(dns_resolver)} {}

std::vector<ObjPtr<Channel>> EthernetAccessPoint::GenerateChannels(
    std::vector<UnifiedAddress> const& endpoints) {
  Aether::ptr aether = aether_;
  DnsResolver::ptr resolver = dns_resolver_;
  IPoller::ptr poller = poller_;

  std::vector<ObjPtr<Channel>> channels;
  channels.reserve(endpoints.size());
  for (auto const& address : endpoints) {
    channels.emplace_back(
        domain_->CreateObj<EthernetChannel>(aether, resolver, poller, address));
  }
  return channels;
}

}  // namespace ae
