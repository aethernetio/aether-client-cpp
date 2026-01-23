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

namespace ae {
// ========================WiFi init========================================
struct WiFiIP {
  AE_REFLECT_MEMBERS(static_ip_v4, gateway_v4, netmask_v4, primary_dns_v4,
                     secondary_dns_v4, static_ip_v6)
  IpV4Addr static_ip_v4{};  // ESP32 static IP v4
  IpV4Addr gateway_v4{};    // IP Address of your network gateway (router)
  IpV4Addr netmask_v4{};    // Netmask
  std::optional<IpV4Addr> primary_dns_v4{};    // Primary DNS (optional)
  std::optional<IpV4Addr> secondary_dns_v4{};  // Secondary DNS (optional)
  std::optional<IpV6Addr> static_ip_v6{};      // ESP32 static IP v6
};

struct WifiCreds {
  AE_REFLECT_MEMBERS(ssid, password)

  std::string ssid;
  std::string password;
};

struct WiFiAp {
  AE_REFLECT_MEMBERS(creds, static_ip)
  WifiCreds creds;
  std::optional<WiFiIP>
      static_ip;  // use static ip settings, otherwise use dynamic settings
};

struct WiFiPowerSaveParam {
  AE_REFLECT_MEMBERS(ps_enabled, wifi_ps_type, protocol_bitmap, listen_interval,
                     beacon_interval);
  bool ps_enabled{true};
  uint8_t wifi_ps_type;
  uint8_t protocol_bitmap;
  int16_t listen_interval;
  uint16_t beacon_interval;
};

struct WiFiBaseStation {
  AE_REFLECT_MEMBERS(connected, target_bssid, target_channel)
  bool connected{false};
  uint8_t target_bssid[6];
  uint8_t target_channel;
};

struct WiFiInit {
  AE_REFLECT_MEMBERS(wifi_ap, psp)
  std::vector<WiFiAp> wifi_ap;
  WiFiPowerSaveParam psp;
};

}  // namespace ae
#endif
#endif  // AETHER_WIFI_WIFI_DRIVER_TYPES_H_
