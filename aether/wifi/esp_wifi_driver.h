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

#ifndef AETHER_WIFI_ESP_WIFI_DRIVER_H_
#define AETHER_WIFI_ESP_WIFI_DRIVER_H_

#include "aether/config.h"  // IWYU pragma: keep

#if (defined(ESP_PLATFORM))
#  define ESP_WIFI_DRIVER_ENABLED 1
#  include <memory>
#  include <optional>

#  include "aether/wifi/wifi_driver.h"

namespace ae {
class EspWifiDriver final : public WifiDriver {
 public:
  EspWifiDriver();
  ~EspWifiDriver() override;

  void Connect(WifiCreds const& creds) override;
  WifiCreds connected_to() const override;

 private:
  void Disconnect();
  void Init();
  void InitNvs();
  void Deinit();

  static int initialized_;
  static void* espt_init_sta_;

  std::optional<WifiCreds> connected_to_;
  std::unique_ptr<struct ConnectionState> connection_state_;
};
}  // namespace ae
#endif
#endif  // AETHER_WIFI_ESP_WIFI_DRIVER_H_
