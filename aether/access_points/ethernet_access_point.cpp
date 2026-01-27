
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
#include "aether/server.h"
#include "aether/poller/poller.h"
#include "aether/dns/dns_resolve.h"
#include "aether/channels/ethernet_channel.h"
#include "aether/access_points/filter_endpoints.h"

namespace ae {
EthernetAccessPoint::EthernetAccessPoint(ObjProp prop, ObjPtr<Aether> aether,
                                         ObjPtr<IPoller> poller,
                                         ObjPtr<DnsResolver> dns_resolver)
    : AccessPoint{prop},
      aether_{std::move(aether)},
      poller_{std::move(poller)},
      dns_resolver_{std::move(dns_resolver)} {}

std::vector<ObjPtr<Channel>> EthernetAccessPoint::GenerateChannels(
    ObjPtr<Server> const& server) {
  Aether::ptr aether = aether_;
  DnsResolver::ptr resolver = dns_resolver_;
  IPoller::ptr poller = poller_;

  auto const& s = server.Load();
  std::vector<ObjPtr<Channel>> channels;
  channels.reserve(s->endpoints.size());
  for (auto const& endpoint : s->endpoints) {
    if (!FilterAddresses<AddrVersion::kIpV4, AddrVersion::kIpV6,
                         AddrVersion::kNamed>(endpoint)) {
      continue;
    }
    if (!FilterProtocol<Protocol::kTcp, Protocol::kUdp>(endpoint)) {
      continue;
    }
    channels.emplace_back(EthernetChannel::ptr::Create(domain, aether, resolver,
                                                       poller, endpoint));
  }
  return channels;
}

}  // namespace ae
