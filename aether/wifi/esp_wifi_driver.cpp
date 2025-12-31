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

#include "aether/wifi/esp_wifi_driver.h"

#if defined ESP_WIFI_DRIVER_ENABLED

#  include <string.h>

#  include "nvs_flash.h"

#  include "esp_log.h"
#  include "esp_wifi.h"
#  include "esp_event.h"
#  include "esp_system.h"

#  include "freertos/task.h"
#  include "freertos/FreeRTOS.h"
#  include "freertos/event_groups.h"

#  include "lwip/err.h"
#  include "lwip/sys.h"

#  include "aether/tele/tele.h"

namespace ae {
#  define WIFI_CONNECTED_BIT BIT0
#  define WIFI_FAIL_BIT BIT1

struct ConnectionState {
  EventGroupHandle_t event_group;
  std::size_t retry_count{};
};

namespace esp_wifi_driver_internal {
static constexpr int kMaxRetry = 10;
void EventHandler(void* arg, esp_event_base_t event_base, int32_t event_id,
                  void* event_data) {
  ConnectionState* connection_state = static_cast<ConnectionState*>(arg);
  if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
    esp_wifi_connect();
  } else if ((event_base == WIFI_EVENT) &&
             (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
    if (connection_state->retry_count < kMaxRetry) {
      esp_wifi_connect();
      connection_state->retry_count++;
    } else {
      xEventGroupSetBits(connection_state->event_group, WIFI_FAIL_BIT);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(connection_state->event_group, WIFI_CONNECTED_BIT);
  }
}
}  // namespace esp_wifi_driver_internal

int EspWifiDriver::initialized_ = 0;
void* EspWifiDriver::espt_init_sta_ = nullptr;

EspWifiDriver::EspWifiDriver() {
  if (initialized_++ > 0) {
    return;
  }
  Init();
}

EspWifiDriver::~EspWifiDriver() {
  if (connected_to_) {
    Disconnect();
  }
  if (--initialized_ > 0) {
    return;
  }
  Deinit();
}

void EspWifiDriver::Connect(WifiCreds const& creds) {
  if (connected_to_) {
    Disconnect();
  }

  connected_to_.reset();

  connection_state_ = std::make_unique<ConnectionState>();
  connection_state_->event_group = xEventGroupCreate();

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, esp_wifi_driver_internal::EventHandler,
      connection_state_.get(), &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, esp_wifi_driver_internal::EventHandler,
      connection_state_.get(), &instance_got_ip));

  wifi_scan_threshold_t wifi_threshold{};
  wifi_threshold.rssi = 0;
  wifi_threshold.authmode = WIFI_AUTH_WPA2_PSK;

  wifi_config_t wifi_config{};
  wifi_config.sta.threshold = wifi_threshold;

  // for debug purpose only, it's private data
  AE_TELED_DEBUG("Connecting to ap SSID:{} PSWD:{}", creds.ssid,
                 creds.password);

  strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), creds.ssid.data(),
          sizeof(wifi_config.sta.ssid));
  strncpy(reinterpret_cast<char*>(wifi_config.sta.password),
          creds.password.data(), sizeof(wifi_config.sta.password));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
  ESP_ERROR_CHECK(esp_wifi_set_protocol(
      WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
  ESP_ERROR_CHECK(esp_wifi_start());

  AE_TELED_DEBUG("WifiInitSta finished.");

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or
   * connection failed for the maximum number of re-tries (WIFI_FAIL_BIT). The
   * bits are set by EventHandler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(connection_state_->event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we
   * can test which event actually happened. */
  if (bits & WIFI_CONNECTED_BIT) {
    AE_TELED_DEBUG("Connected to AP");
    connected_to_ = creds;
  } else if (bits & WIFI_FAIL_BIT) {
    AE_TELED_DEBUG("Failed to connect to AP");
  } else {
    AE_TELED_DEBUG("UNEXPECTED EVENT {}", static_cast<int>(bits));
  }

  /* The event will not be processed after unregister */
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
  vEventGroupDelete(connection_state_->event_group);
  connection_state_.reset();
}

WifiCreds EspWifiDriver::connected_to() const {
  if (!connected_to_) {
    return {};
  }
  return *connected_to_;
}

void EspWifiDriver::Init() {
  InitNvs();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  espt_init_sta_ = esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
}

void EspWifiDriver::InitNvs() {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
}

void EspWifiDriver::Deinit() {
  esp_wifi_deinit();
  esp_netif_destroy_default_wifi(static_cast<esp_netif_t*>(espt_init_sta_));
}

void EspWifiDriver::Disconnect() {
  esp_wifi_disconnect();
  esp_wifi_stop();
}
}  // namespace ae
#endif
