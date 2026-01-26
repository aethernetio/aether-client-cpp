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
#  include "lwip/ip4_addr.h"
#  include "lwip/ip6_addr.h"

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

void EspWifiDriver::Connect(WiFiAp const& wifi_ap,
                            WiFiPowerSaveParam const& psp,
                            WiFiBaseStation& base_station_) {
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
  wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();

  // Restore saved Base Station
  if (base_station_.connected) {
    wifi_config.sta.scan_method = WIFI_FAST_SCAN;  // Fast scan
    wifi_config.sta.bssid_set = true;              // Enable BSSID binding
    wifi_config.sta.channel = base_station_.target_channel;  // Set channel
    // Copy the BSSID to the configuration
    memcpy(wifi_config.sta.bssid, base_station_.target_bssid, 6);
    ESP_ERROR_CHECK(esp_wifi_set_channel(base_station_.target_channel,
                                         WIFI_SECOND_CHAN_NONE));
  }

  wifi_config.sta.threshold = wifi_threshold;
  if (psp.ps_enabled) {
    wifi_config.sta.listen_interval = psp.listen_interval;
  }

  // for debug purpose only, it's private data
  AE_TELED_DEBUG("Connecting to ap SSID:{} PSWD:{}", wifi_ap.creds.ssid,
                 wifi_ap.creds.password);

  strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid),
          wifi_ap.creds.ssid.data(), sizeof(wifi_config.sta.ssid));
  strncpy(reinterpret_cast<char*>(wifi_config.sta.password),
          wifi_ap.creds.password.data(), sizeof(wifi_config.sta.password));

  // Setting up a static IP, if required
  if (wifi_ap.static_ip.has_value()) {
    esp_err_t err = SetStaticIp(static_cast<esp_netif_t*>(espt_init_sta_),
                                wifi_ap.static_ip.value());
    if (err != ESP_OK) {
      AE_TELED_ERROR("Failed to set static IP, falling back to DHCP");
      // If an error occurs, switch to DHCP
    }
  } else {
    AE_TELED_DEBUG("Using DHCP for IP configuration");
  }

  // We disable aggregation so that the packages go out one by one and quickly
  wifi_init_config.ampdu_rx_enable = 0;
  wifi_init_config.ampdu_tx_enable = 0;

  ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(
      esp_wifi_set_ps(static_cast<wifi_ps_type_t>(psp.wifi_ps_type)));
  ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, psp.protocol_bitmap));
  ESP_ERROR_CHECK(esp_wifi_start());

  AE_TELED_DEBUG("WifiInitSta finished.");

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT)
   * or connection failed for the maximum number of re-tries (WIFI_FAIL_BIT).
   * The bits are set by EventHandler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(connection_state_->event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we
   * can test which event actually happened. */
  if (bits & WIFI_CONNECTED_BIT) {
    AE_TELED_DEBUG("Connected to AP");
    connected_to_ = wifi_ap.creds;
    // Save Base Station
    base_station_.connected = true;
    base_station_.target_channel = wifi_config.sta.channel;  // Set channel
    // Copy the BSSID to the configuration
    memcpy(base_station_.target_bssid, wifi_config.sta.bssid, 6);
  } else if (bits & WIFI_FAIL_BIT) {
    AE_TELED_DEBUG("Failed to connect to AP, trying next AP");
    Disconnect();
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

esp_err_t EspWifiDriver::SetStaticIp(esp_netif_t* netif, WiFiIP const& config) {
  esp_err_t err = ESP_OK;

#  if AE_SUPPORT_IPV4 == 1
  esp_netif_ip_info_t ip_info;
  esp_netif_dns_info_t dns_info1, dns_info2;

  // Conversion to IP addresses
  memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));
  memset(&dns_info1, 0, sizeof(esp_netif_dns_info_t));
  memset(&dns_info2, 0, sizeof(esp_netif_dns_info_t));

  auto static_ip = config.static_ip_v4;
  IP4_ADDR(&ip_info.ip, static_ip.ipv4_value[0], static_ip.ipv4_value[1],
           static_ip.ipv4_value[2], static_ip.ipv4_value[3]);
  auto gateway = config.gateway_v4;
  IP4_ADDR(&ip_info.gw, gateway.ipv4_value[0], gateway.ipv4_value[1],
           gateway.ipv4_value[2], gateway.ipv4_value[3]);
  auto netmask = config.netmask_v4;
  IP4_ADDR(&ip_info.netmask, netmask.ipv4_value[0], netmask.ipv4_value[1],
           netmask.ipv4_value[2], netmask.ipv4_value[3]);
  if (config.primary_dns_v4.has_value()) {
    auto primary_dns = config.primary_dns_v4.value();
    IP4_ADDR(&dns_info1.ip.u_addr.ip4, primary_dns.ipv4_value[0],
             primary_dns.ipv4_value[1], primary_dns.ipv4_value[2],
             primary_dns.ipv4_value[3]);
  }
  if (config.secondary_dns_v4.has_value()) {
    auto secondary_dns = config.secondary_dns_v4.value();
    IP4_ADDR(&dns_info2.ip.u_addr.ip4, secondary_dns.ipv4_value[0],
             secondary_dns.ipv4_value[1], secondary_dns.ipv4_value[2],
             secondary_dns.ipv4_value[3]);
  }
#  endif
#  if AE_SUPPORT_IPV6 == 1
  if (config.static_ip_v6.has_value()) {
    esp_netif_ip6_info_t ip_info_v6;

    memset(&ip_info_v6, 0, sizeof(esp_netif_ip6_info_t));

    std::array<std::uint32_t const*, 4> ip6_parts{};
    for (auto i = 0; i < 4; ++i) {
      ip6_parts[i] = reinterpret_cast<std::uint32_t const*>(
          &config.static_ip_v6.value().ipv6_value[i * 4]);
    }
    IP6_ADDR(&ip_info_v6.ip, PP_HTONL(*ip6_parts[0]), PP_HTONL(*ip6_parts[1]),
             PP_HTONL(*ip6_parts[2]), PP_HTONL(*ip6_parts[3]));
  }
#  endif

  // Stopping the DHCP client
  err = esp_netif_dhcpc_stop(netif);
  if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
    AE_TELED_ERROR("Failed to stop DHCP client: {}", esp_err_to_name(err));
    return err;
  }

#  if AE_SUPPORT_IPV4 == 1
  // Setting a static IP
  err = esp_netif_set_ip_info(netif, &ip_info);
  if (err != ESP_OK) {
    AE_TELED_ERROR("Failed to set IP info: {}", esp_err_to_name(err));
    return err;
  }

  // Installing DNS servers
  if (config.primary_dns_v4.has_value()) {
    err = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info1);
    if (err != ESP_OK) {
      AE_TELED_ERROR("Failed to set primary DNS: {}", esp_err_to_name(err));
    }
  }

  if (config.secondary_dns_v4.has_value()) {
    err = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns_info2);
    if (err != ESP_OK) {
      AE_TELED_ERROR("Failed to set secondary DNS: {}", esp_err_to_name(err));
    }
  }

  AE_TELED_DEBUG("Static IP V4 configured: {}", config.static_ip_v4);
#  endif
#  if AE_SUPPORT_IPV6 == 1
  if (config.static_ip_v6.has_value()) {
    err = esp_netif_set_ip6_global(netif, &ip_info_v6.ip);
    if (err != ESP_OK) {
      AE_TELED_ERROR("Failed to set IP V6 info: {}", esp_err_to_name(err));
      return err;
    }

    AE_TELED_DEBUG("Static IP V6 configured: {}", config.static_ip_v6);
  }
#  endif

  return ESP_OK;
}

}  // namespace ae
#endif
