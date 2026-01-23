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
static constexpr std::string kWifi1Ssid = "Test1234";
static constexpr std::string kWifi1Pass = "Test1234";

static constexpr std::string kWifi2Ssid = "Test2345";
static constexpr std::string kWifi2Pass = "Test2345";

static IpV4Addr my_static_ip_v4{192, 168, 1, 215};
static IpV4Addr my_gateway_ip_v4{192, 168, 1, 1};
static IpV4Addr my_netmask_ip_v4{255, 255, 255, 0};
static IpV4Addr my_dns1_ip_v4{8, 8, 8, 8};
static IpV4Addr my_dns2_ip_v4{8, 8, 4, 4};
static IpV6Addr my_static_ip_v6{0x20, 0x01, 0x0d, 0xb8, 0x85, 0xa3, 0x00, 0x00,
                                0x00, 0x00, 0x8a, 0x2e, 0x03, 0x70, 0x73, 0x34};

WiFiIP wifi_ip{
    {my_static_ip_v4},   // ESP32 static IP
    {my_gateway_ip_v4},  // IP Address of your network gateway (router)
    {my_netmask_ip_v4},  // Subnet mask
    {my_dns1_ip_v4},     // Primary DNS (optional)
    {my_dns2_ip_v4},     // Secondary DNS (optional)
    {my_static_ip_v6}    // ESP32 static IP v6
};

static WifiCreds my_wifi1{kWifi1Ssid, kWifi1Pass};
static WifiCreds my_wifi2{kWifi2Ssid, kWifi2Pass};

ae::WiFiAp wifi1_ap{my_wifi1, wifi_ip};
ae::WiFiAp wifi2_ap{my_wifi2, wifi_ip};
                    
std::vector<ae::WiFiAp> wifi_ap_vec{wifi1_ap, wifi2_ap};

static WiFiPowerSaveParam wifi_psp{
    true,
    AE_WIFI_PS_MAX_MODEM,  // Power save type
    AE_WIFI_PROTOCOL_11B | AE_WIFI_PROTOCOL_11G |
        AE_WIFI_PROTOCOL_11N,  // Protocol bitmap
    3,                         // Listen interval
    500                        // Beacon interval
};

WiFiInit wifi_init{
    wifi_ap_vec, // Wi-Fi access points
    wifi_psp,    // Power save parameters
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
                context.dns_resolver(), wifi_init));
            return adapter_registry;
          })
#  endif
  );
}

}  // namespace ae::cloud_test

#endif
#endif  // CLOUD_AETHER_CONSTRUCT_ESP_WIFI_H_
