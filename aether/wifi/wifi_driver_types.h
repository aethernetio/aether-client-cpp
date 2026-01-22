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

#ifndef AETHER_WIFI_WIFI_DRIVER_TYPES_H_
#define AETHER_WIFI_WIFI_DRIVER_TYPES_H_

#include "aether/config.h"

#if AE_SUPPORT_WIFIS

#  include <string>
#  include <vector>

#  include "aether/reflect/reflect.h"
#  include "aether/types/address.h"
#  include "aether/types/variant_type.h"

#  if (defined(ESP_PLATFORM))
#    include "esp_wifi_types_generic.h"
#  endif

namespace ae {
// ========================WiFi init========================================
struct WiFiIP {
  AE_REFLECT_MEMBERS(use_dhcp, static_ip_v4, gateway_v4, netmask_v4,
                     primary_dns_v4, secondary_dns_v4, use_ipv6, static_ip_v6)
  bool use_dhcp{false};
  Address static_ip_v4{};      // ESP32 static IP v4
  Address gateway_v4{};        // IP Address of your network gateway (router)
  Address netmask_v4{};        // Netmask
  Address primary_dns_v4{};    // Primary DNS (optional)
  Address secondary_dns_v4{};  // Secondary DNS (optional)
  bool use_ipv6{false};        // Use IPV6
  Address static_ip_v6{};      // ESP32 static IP v6
};

struct WifiCreds {
  AE_REFLECT_MEMBERS(ssid, password, wifi_ip)

  std::string ssid;
  std::string password;
  WiFiIP wifi_ip;
};

#  if (defined(ESP_PLATFORM))
struct WiFiPowerSaveParam {
  AE_REFLECT_MEMBERS(ps_enabled, wifi_ps_type, protocol_bitmap, listen_interval,
                     beacon_interval);
  bool ps_enabled{true};
  wifi_ps_type_t wifi_ps_type;
  uint8_t protocol_bitmap;
  int16_t listen_interval;
  uint16_t beacon_interval;
};
#  else
struct WiFiPowerSaveParam {
  AE_REFLECT_MEMBERS(ps_enabled)
  bool ps_enabled{true};
};
#  endif

struct WiFiBaseStation {
  AE_REFLECT_MEMBERS(creds, connected, target_bssid, target_channel)
  WifiCreds creds;
  bool connected{false};
  uint8_t target_bssid[6];
  uint8_t target_channel;
};

struct WiFiInit {
  AE_REFLECT_MEMBERS(wifi_creds, psp, bs)
  std::vector<WifiCreds> wifi_creds;
  WiFiPowerSaveParam psp;
  WiFiBaseStation bs;
};

}  // namespace ae
#endif
#endif  // AETHER_WIFI_WIFI_DRIVER_TYPES_H_
