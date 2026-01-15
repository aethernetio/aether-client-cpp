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

#ifndef CLOUD_AETHER_CONSTRUCT_ESP_WIFI_H_
#define CLOUD_AETHER_CONSTRUCT_ESP_WIFI_H_

#include "aether_construct.h"

#if CLOUD_TEST_ESP_WIFI

namespace ae::cloud_test {
static constexpr std::string kWifiSsid = "Test1234";
static constexpr std::string kWifiPass = "Test1234";

WifiCreds my_wifi{kWifiSsid, kWifiPass};

std::vector<WifiCreds> wifi_creds{my_wifi};

IpV4Addr my_static_ip{192, 168, 1, 100};

static ae::WiFiInit const wifi_init{
  wifi_creds,         // Wi-Fi credentials
  {},                 // Power save parameters
  {},                 // Base station
  true,               // Use DHCP
  {my_static_ip},     // ESP32 static IP
  {},                 // IP Address of your network gateway (router)
  {},                 // Subnet mask
  {},                 // Primary DNS (optional)
  {}                  // Secondary DNS (optional)
};

RcPtr<AetherApp> construct_aether_app() {
  return AetherApp::Construct(
      AetherAppContext{}
#  if defined AE_DISTILLATION
          .AdaptersFactory([](AetherAppContext const& context) {
            auto adapter_registry =
                context.domain().CreateObj<AdapterRegistry>();
            adapter_registry->Add(context.domain().CreateObj<WifiAdapter>(
                GlobalId::kWiFiAdapter, context.aether(), context.poller(),
                context.dns_resolver(), std::string(kWifiSsid),
                std::string(kWifiPass)));
            return adapter_registry;
          })
#  endif
  );
}

}  // namespace ae::cloud_test

#endif
#endif  // CLOUD_AETHER_CONSTRUCT_ESP_WIFI_H_
