
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

#include "aether/server.h"
#include "aether/poller/poller.h"
#include "aether/dns/dns_resolve.h"
#include "aether/channels/ethernet_channel.h"
#include "aether/access_points/filter_endpoints.h"

namespace ae {
EthernetAccessPoint::EthernetAccessPoint(Aether& aether, IPoller& poller,
                                         DnsResolver& dns_resolver,
                                         Domain* domain)
    : AccessPoint{domain},
      aether_{&aether},
      poller_{&poller},
      dns_resolver_{&dns_resolver} {}

std::vector<std::unique_ptr<Channel>> EthernetAccessPoint::GenerateChannels(
    Server& server) {
  std::vector<std::unique_ptr<Channel>> channels;
  channels.reserve(server.endpoints.size());
  for (auto const& endpoint : server.endpoints) {
    if (!FilterAddresses<AddrVersion::kIpV4, AddrVersion::kIpV6,
                         AddrVersion::kNamed>(endpoint)) {
      continue;
    }
    if (!FilterProtocol<Protocol::kTcp, Protocol::kUdp>(endpoint)) {
      continue;
    }
    channels.emplace_back(std::make_unique<EthernetChannel>(
        *aether_, *dns_resolver_, *poller_, endpoint, domain_));
  }
  return channels;
}

}  // namespace ae
