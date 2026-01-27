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

#include "aether/adapters/wifi_adapter.h"

#include <utility>
#include <cassert>

#include "aether/aether.h"
#include "aether/poller/poller.h"
#include "aether/dns/dns_resolve.h"
#include "aether/wifi/wifi_driver_factory.h"
#include "aether/access_points/wifi_access_point.h"

namespace ae {
#if defined AE_DISTILLATION
WifiAdapter::WifiAdapter(ObjProp prop, ObjPtr<Aether> aether,
                         IPoller::ptr poller, DnsResolver::ptr dns_resolver,
                         WiFiInit wifi_init)
    : ParentWifiAdapter{prop, std::move(aether), std::move(poller),
                        std::move(dns_resolver), std::move(wifi_init)} {
  AE_TELED_DEBUG("Wifi instance created!");
}
#endif  // AE_DISTILLATION

std::vector<AccessPoint::ptr> WifiAdapter::access_points() {
  if (access_points_.size() == 0) {
    Aether::ptr aether = aether_;
    DnsResolver::ptr dns_resolver = dns_resolver_;
    IPoller::ptr poller = poller_;
    auto self_ptr = WifiAdapter::ptr::MakeFromThis(this);

    for (const auto& ap : wifi_init_.wifi_ap) {
      auto access_point = WifiAccessPoint::ptr::Create(
          domain, aether, self_ptr, poller, dns_resolver, ap, wifi_init_.psp);
      access_points_.emplace_back(std::move(access_point));
    }
  }

  return access_points_;
}

WifiDriver& WifiAdapter::driver() {
  if (!wifi_driver_) {
    wifi_driver_ = WifiDriverFactory::CreateWifiDriver();
  }
  return *wifi_driver_;
}

}  // namespace ae
