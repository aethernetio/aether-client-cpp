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

#ifndef AETHER_ADAPTERS_WIFI_ADAPTER_H_
#define AETHER_ADAPTERS_WIFI_ADAPTER_H_

#include <string>
#include <memory>

#include "aether/wifi/wifi_driver.h"
#include "aether/adapters/parent_wifi.h"

namespace ae {

class WifiAdapter final : public ParentWifiAdapter {
  AE_OBJECT(WifiAdapter, ParentWifiAdapter, 0)
  WifiAdapter() = default;

 public:
#ifdef AE_DISTILLATION
  WifiAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
              DnsResolver::ptr dns_resolver, std::string ssid, std::string pass,
              Domain* domain);
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT(AE_MMBRS(access_point_))

  std::vector<AccessPoint::ptr> access_points() override;

  WifiDriver& driver();

 private:
  std::unique_ptr<WifiDriver> wifi_driver_;
  AccessPoint::ptr access_point_;
};
}  // namespace ae
#endif  // AETHER_ADAPTERS_WIFI_ADAPTER_H_
