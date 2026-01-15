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

#    if (defined(ESP_PLATFORM))
#  include "esp_wifi_types_generic.h"
#    endif

namespace ae {
// ========================WiFi init========================================
struct WifiCreds {
  AE_REFLECT_MEMBERS(ssid, password)

  std::string ssid;
  std::string password;
};

#    if (defined(ESP_PLATFORM))
struct WiFiPowerSaveParam {
  AE_REFLECT_MEMBERS(wifi_ps_type, protocol_bitmap, listen_interval,
                     beacon_interval)
  wifi_ps_type_t wifi_ps_type;
  uint8_t protocol_bitmap;
  int16_t listen_interval;
  uint16_t beacon_interval;
};
#    else
struct WiFiPowerSaveParam {};
#    endif

struct WiFiBaseStation {
  AE_REFLECT_MEMBERS(creds, channel)
  WifiCreds creds;
  uint8_t channel;
};

struct WiFiInit {
  AE_REFLECT_MEMBERS(wifi_creds, psp, bs, use_dhcp, static_ip, gateway, subnet,
                     primary_dns, secondary_dns)
  std::vector<WifiCreds> wifi_creds;
  WiFiPowerSaveParam psp;
  WiFiBaseStation bs;
  bool use_dhcp{true};
  Address static_ip{};      // ESP32 static IP
  Address gateway{};        // IP Address of your network gateway (router)
  Address subnet{};         // Subnet mask
  Address primary_dns{};    // Primary DNS (optional)
  Address secondary_dns{};  // Secondary DNS (optional)
};

}  // namespace ae
#endif
#endif  // AETHER_WIFI_WIFI_DRIVER_TYPES_H_
