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

#if (defined(ESP_PLATFORM)) && AE_SUPPORT_WIFIS && AE_ENABLE_ESP32_WIFI
#  define ESP_WIFI_DRIVER_ENABLED 1
#  include <optional>

#  include "freertos/FreeRTOS.h"

#  include "freertos/task.h"
#  include "freertos/event_groups.h"

#  include "esp_err.h"
#  include "esp_netif_types.h"

#  include "aether/ae_context.h"
#  include "aether/wifi/wifi_driver.h"

namespace ae {
namespace esp_wifi_driver_internal {
void EventHandler(void* arg, esp_event_base_t event_base, int32_t event_id,
                  void* event_data);
}

class EspWifiDriver final : public WifiDriver {
  // friend with wifi event handler
  friend void esp_wifi_driver_internal::EventHandler(
      void* arg, esp_event_base_t event_base, int32_t event_id,
      void* event_data);

 public:
  enum class State : char {
    kDisconnected,
    kDisconnecring,
    kConnecting,
    kConnected,
  };

  struct ConnectionState {
    State state;
    std::size_t retry_count{};
  };

  explicit EspWifiDriver(AeContext const& ae_context);
  ~EspWifiDriver() override;

  void Connect(WiFiAp const& wifi_ap,
               std::optional<WiFiPowerSaveParam> const& psp,
               std::optional<WiFiBaseStation> const& base_station_) override;

  ConnectResEvent::Subscriber connect_res_event() override;

  std::optional<std::string> connected_to() const override;

 private:
  void Init();
  static void InitNvs();
  void Deinit();

  void Disconnect();

  void ConnectingEventHandler(esp_event_base_t event_base, int32_t event_id,
                              void* event_data);
  void ConnectedEventHandler(esp_event_base_t event_base, int32_t event_id,
                             void* event_data);
  void DisconnectingEventHandler(esp_event_base_t event_base, int32_t event_id,
                                 void* event_data);
  void DisconnectedEventHandler(esp_event_base_t event_base, int32_t event_id,
                                void* event_data);

  AeContext ae_context_;
  ConnectResEvent connect_res_event_;

  ConnectionState connection_state_;
  std::optional<std::string> connected_to_;
  TaskSubscription event_task_sub_;

  // driver could be initialized only once
  void* espt_init_sta_ = nullptr;
};
}  // namespace ae
#endif
#endif  // AETHER_WIFI_ESP_WIFI_DRIVER_H_
