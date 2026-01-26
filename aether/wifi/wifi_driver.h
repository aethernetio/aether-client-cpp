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

#include <string>

#include "aether/reflect/reflect.h"
#include "aether/wifi/wifi_driver_types.h"

namespace ae {
/**
 * \brief A wifi driver interface.
 */
class WifiDriver {
 public:
  virtual ~WifiDriver() = default;

  /**
   * \brief Connect to an access point with creds.
   */
  virtual void Connect(WiFiAp const& wifi_ap, WiFiPowerSaveParam const& psp,
                       WiFiBaseStation& base_station_) = 0;
  /**
   * \brief Get creds for currently connected access point.
   * \return if connected WifiCreds with filled at least ssid, otherwise empty.
   */
  virtual WifiCreds connected_to() const = 0;
};
}  // namespace ae

#endif  // AETHER_WIFI_WIFI_DRIVER_H_
