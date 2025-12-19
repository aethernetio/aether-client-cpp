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

#ifndef REGISTRATOR_REGISTER_WIFI_H_
#define REGISTRATOR_REGISTER_WIFI_H_

#include "aether/adapters/ethernet.h"
#include "aether/adapters/parent_wifi.h"

namespace ae::registrator {
class RegisterWifiAdapter : public ParentWifiAdapter {
  AE_OBJECT(RegisterWifiAdapter, ParentWifiAdapter, 0)

 public:
  RegisterWifiAdapter(Aether& aether, IPoller& poller,
                      DnsResolver& dns_resolver, std::string ssid,
                      std::string pass, Domain* domain);

  std::vector<AccessPoint*> access_points() override;
  NewAccessPoint::Subscriber new_access_point() override;

 private:
  // whose doing all job
  EthernetAdapter ethernet_adapter_;
};
}  // namespace ae::registrator

#endif  // REGISTRATOR_REGISTER_WIFI_H_
