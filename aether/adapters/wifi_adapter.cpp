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
WifiAdapter::WifiAdapter(Aether& aether, IPoller& poller,
                         DnsResolver& dns_resolver, std::string ssid,
                         std::string pass, Domain* domain)
    : ParentWifiAdapter{aether,          poller,          dns_resolver,
                        std::move(ssid), std::move(pass), domain},
      wifi_driver_{WifiDriverFactory::CreateWifiDriver()},
      access_point_{
          aether, *this, poller, dns_resolver, WifiCreds{ssid_, pass_}, domain_,
      } {
  AE_TELED_DEBUG("Wifi instance created!");
}

std::vector<AccessPoint*> WifiAdapter::access_points() {
  return {&access_point_};
}

WifiDriver& WifiAdapter::driver() { return *wifi_driver_; }

}  // namespace ae
