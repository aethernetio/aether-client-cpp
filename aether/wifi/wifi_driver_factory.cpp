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

#include "aether/wifi/wifi_driver_factory.h"

// IWYU pragma: begin_keeps
#include "aether/wifi/esp_wifi_driver.h"
// IWYU pragma: end_keeps

namespace ae {
std::unique_ptr<WifiDriver> WifiDriverFactory::CreateWifiDriver() {
#if defined ESP_WIFI_DRIVER_ENABLED
  return std::make_unique<EspWifiDriver>();
#else
  return nullptr;
#endif
}

}  // namespace ae
