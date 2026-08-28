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

#ifndef AETHER_WIFI_WIFI_DRIVER_H_
#define AETHER_WIFI_WIFI_DRIVER_H_

#include "aether/config.h"

#if AE_SUPPORT_WIFIS
#  include "aether-miscpp/types/result.h"
#  include "aether/events/events.h"
#  include "aether/wifi/wifi_driver_types.h"

namespace ae {
/**
 * \brief A wifi driver interface.
 */
class WifiDriver {
 public:
  struct ConnectOk {};

  /**
   * \brief Wifi connection result (success or error code).
   */
  using ConnectResEvent = Event<void(Result<ConnectOk, int>&& res)>;

  virtual ~WifiDriver() = default;

  /**
   * \brief Connect to an access point with SSID/password (and optional PSP).
   *
   * Generic Aether Wi-Fi does not use BSSID or channel caches.
   */
  virtual void Connect(WiFiAp const& wifi_ap,
                       std::optional<WiFiPowerSaveParam> const& psp) = 0;

  virtual ConnectResEvent::Subscriber connect_res_event() = 0;

  /**
   * \brief Get the AP ssid if connected
   */
  virtual std::optional<std::string> connected_to() const = 0;

  // TODO: add disconnected event
};
}  // namespace ae

#endif
#endif  // AETHER_WIFI_WIFI_DRIVER_H_
