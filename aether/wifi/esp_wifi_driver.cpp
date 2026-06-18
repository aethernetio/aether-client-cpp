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
#  include "esp_private/wifi.h"

#  include "lwip/err.h"
#  include "lwip/sys.h"
#  include "lwip/ip4_addr.h"
#  include "lwip/ip6_addr.h"

#  include "aether/tele/tele.h"

extern "C" esp_err_t esp_wifi_internal_set_retry_counter(uint8_t short_retry,
                                                         uint8_t long_retry);
extern "C" esp_err_t esp_wifi_internal_get_fix_rate(wifi_interface_t ifx,
                                                    bool* is_fixed,
                                                    wifi_phy_rate_t* rate);

namespace ae {
#  define WIFI_CONNECTED_BIT BIT0
#  define WIFI_FAIL_BIT BIT1

namespace esp_wifi_driver_internal {
static constexpr int kMaxRetry = 10;
void EventHandler(void* arg, esp_event_base_t event_base, int32_t event_id,
                  [[maybe_unused]] void* event_data) {
  auto base_type = [](esp_event_base_t event_base) {
    if (event_base == WIFI_EVENT) {
      return "WIFI_EVENT";
    }
    if (event_base == IP_EVENT) {
      return "IP_EVENT";
    }
    return "UNKNOWN_EVENT";
  };

  ESP_LOGI("EspWiFiEventHandler", "Event handler event_base %s event_id %d",
           base_type(event_base), event_id);

  auto* driver = static_cast<EspWifiDriver*>(arg);
  switch (driver->connection_state_.state) {
    case EspWifiDriver::State::kDisconnected:
      driver->DisconnectedEventHandler(event_base, event_id, event_data);
      break;
    case EspWifiDriver::State::kDisconnecting:
      driver->DisconnectingEventHandler(event_base, event_id, event_data);
      break;
    case EspWifiDriver::State::kConnecting:
      driver->ConnectingEventHandler(event_base, event_id, event_data);
      break;
    case EspWifiDriver::State::kConnected:
      driver->ConnectedEventHandler(event_base, event_id, event_data);
      break;
  }
}

void SetupBssid(wifi_config_t& wifi_config,
                WiFiBaseStation const& base_station) {
  std::array<uint8_t, sizeof(base_station.target_bssid)> debug_bssid;
  memcpy(debug_bssid.data(), base_station.target_bssid,
         sizeof(base_station.target_bssid));
  AE_TELED_DEBUG("Restored from cash BSSID:{} CHN:{}", debug_bssid,
                 static_cast<int>(base_station.target_channel));

  wifi_config.sta.scan_method = WIFI_FAST_SCAN;  // Fast scan
  wifi_config.sta.bssid_set = true;              // Enable BSSID binding
  wifi_config.sta.channel = base_station.target_channel;  // Set channel
  // Copy the BSSID to the configuration
  memcpy(wifi_config.sta.bssid, base_station.target_bssid,
         sizeof(base_station.target_bssid));
  ESP_ERROR_CHECK(
      esp_wifi_set_channel(base_station.target_channel, WIFI_SECOND_CHAN_NONE));
}

void SetupCredentials(wifi_config_t& wifi_config, WifiCreds const& creds) {
#  ifdef DEBUG
  // for debug purpose only, it's private data
  AE_TELED_DEBUG("Connecting to ap SSID:{} PSWD:{}", creds.ssid,
                 creds.password);
#  endif  // DEBUG

  strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), creds.ssid.data(),
          sizeof(wifi_config.sta.ssid));
  strncpy(reinterpret_cast<char*>(wifi_config.sta.password),
          creds.password.data(), sizeof(wifi_config.sta.password));
}

void MakeIp4Addr(esp_ip4_addr_t& to, IpV4Addr const& from) {
  IP4_ADDR(&to, from.ipv4_value[0], from.ipv4_value[1], from.ipv4_value[2],
           from.ipv4_value[3]);
}

esp_err_t SetStaticIp(esp_netif_t* netif, WiFiIP const& config) {
  esp_err_t err = ESP_OK;

#  if AE_SUPPORT_IPV4 == 1
  esp_netif_ip_info_t ip_info;
  esp_netif_dns_info_t dns_info1;
  esp_netif_dns_info_t dns_info2;

  // Conversion to IP addresses
  memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));
  memset(&dns_info1, 0, sizeof(esp_netif_dns_info_t));
  memset(&dns_info2, 0, sizeof(esp_netif_dns_info_t));

  MakeIp4Addr(ip_info.ip, config.static_ip_v4);
  MakeIp4Addr(ip_info.gw, config.gateway_v4);
  MakeIp4Addr(ip_info.netmask, config.netmask_v4);

  if (config.primary_dns_v4) {
    MakeIp4Addr(dns_info1.ip.u_addr.ip4, config.primary_dns_v4.value());
  }
  if (config.secondary_dns_v4) {
    MakeIp4Addr(dns_info2.ip.u_addr.ip4, config.secondary_dns_v4.value());
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
  if (config.primary_dns_v4) {
    err = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info1);
    if (err != ESP_OK) {
      AE_TELED_ERROR("Failed to set primary DNS: {}", esp_err_to_name(err));
      return err;
    }
  }

  if (config.secondary_dns_v4) {
    err = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns_info2);
    if (err != ESP_OK) {
      AE_TELED_ERROR("Failed to set secondary DNS: {}", esp_err_to_name(err));
      return err;
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

void StartWifiConnection(esp_netif_t* espt_init_sta, WiFiAp const& wifi_ap,
                         std::optional<WiFiPowerSaveParam> const& psp,
                         std::optional<WiFiBaseStation> const& base_station) {
  wifi_config_t wifi_config{};
  if (base_station) {
    // Restore saved Base Station
    esp_wifi_driver_internal::SetupBssid(wifi_config, *base_station);
  }

  wifi_scan_threshold_t wifi_threshold{};
  wifi_threshold.rssi = 0;
  wifi_threshold.authmode = WIFI_AUTH_WPA2_PSK;

  wifi_config.sta.threshold = wifi_threshold;
  if (psp) {
    wifi_config.sta.listen_interval = psp->listen_interval;
  }

  esp_wifi_driver_internal::SetupCredentials(wifi_config, wifi_ap.creds);

  // Setting up a static IP, if required
  if (wifi_ap.static_ip.has_value()) {
    auto err = esp_wifi_driver_internal::SetStaticIp(espt_init_sta,
                                                     wifi_ap.static_ip.value());
    if (err != ESP_OK) {
      AE_TELED_ERROR("Failed to set static IP, falling back to DHCP");
      // If an error occurs, switch to DHCP
    }
  } else {
    AE_TELED_DEBUG("Using DHCP for IP configuration");
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  if (psp) {
    ESP_ERROR_CHECK(
        esp_wifi_set_ps(static_cast<wifi_ps_type_t>(psp->wifi_ps_type)));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, psp->protocol_bitmap));
  }

  ESP_ERROR_CHECK(esp_wifi_start());

  if (psp) {
    ESP_ERROR_CHECK(esp_wifi_internal_set_fix_rate(
        WIFI_IF_STA, true, static_cast<wifi_phy_rate_t>(psp->fix_rate)));
    ESP_ERROR_CHECK(
        esp_wifi_internal_set_retry_counter(psp->short_retry, psp->long_retry));
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(psp->power));
  }

  AE_TELED_DEBUG("WifiInitSta finished.");
}

}  // namespace esp_wifi_driver_internal

EspWifiDriver::EspWifiDriver(AeContext const& ae_context)
    : ae_context_{ae_context} {
  Init();
}

EspWifiDriver::~EspWifiDriver() {
  if (connected_to_) {
    Disconnect();
  }
  Deinit();
}

void EspWifiDriver::Connect(
    WiFiAp const& wifi_ap, std::optional<WiFiPowerSaveParam> const& psp,
    std::optional<WiFiBaseStation> const& base_station) {
  if (connected_to_) {
    Disconnect();
  }
  connected_to_.reset();

  connection_state_ = {};
  connection_state_.state = State::kConnecting;

  esp_wifi_driver_internal::StartWifiConnection(
      static_cast<esp_netif_t*>(espt_init_sta_), wifi_ap, psp, base_station);
  // the connection result will be handled in ConnectingEventHandler
}

EspWifiDriver::ConnectResEvent::Subscriber EspWifiDriver::connect_res_event() {
  return EventSubscriber{connect_res_event_};
}

std::optional<std::string> EspWifiDriver::connected_to() const {
  return connected_to_;
}

void EspWifiDriver::Init() {
  InitNvs();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  espt_init_sta_ = esp_netif_create_default_wifi_sta();

  wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  // We disable aggregation so that the packages go out one by one and quickly
  wifi_init_config.ampdu_rx_enable = 0;
  wifi_init_config.ampdu_tx_enable = 0;

  ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                             esp_wifi_driver_internal::EventHandler, this);
  esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                             esp_wifi_driver_internal::EventHandler, this);
  connection_state_ = {};
  connection_state_.state = State::kDisconnected;
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

  esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               esp_wifi_driver_internal::EventHandler);
  esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                               esp_wifi_driver_internal::EventHandler);
}

void EspWifiDriver::Disconnect() {
  connection_state_.state = State::kDisconnecting;
  connected_to_.reset();
  esp_wifi_disconnect();
  esp_wifi_stop();
}

void EspWifiDriver::ConnectingEventHandler(esp_event_base_t event_base,
                                           int32_t event_id,
                                           [[maybe_unused]] void* event_data) {
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;
      case WIFI_EVENT_STA_DISCONNECTED: {
        auto* event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
        AE_TELED_DEBUG("Wifi event disconnected, reasone {}",
                       static_cast<int>(event->reason));
        if (connection_state_.retry_count <
            esp_wifi_driver_internal::kMaxRetry) {
          esp_wifi_connect();
          connection_state_.retry_count++;
        } else {
          event_task_sub_ = ae_context_.scheduler().Task([&]() {
            Disconnect();
            // connection failed
            connect_res_event_.Emit(Error(1));
          });
        }
        break;
      }
      default:
        break;
    }
  } else if (event_base == IP_EVENT) {
    switch (event_id) {
      case IP_EVENT_STA_GOT_IP:
      case IP_EVENT_GOT_IP6: {
        // Successfully connected
        // get AP info
        wifi_ap_record_t ap_info{};
        esp_wifi_sta_get_ap_info(&ap_info);
        // save real SSID
        connected_to_ = std::string(reinterpret_cast<char*>(ap_info.ssid));
        AE_TELED_DEBUG("Connected to AP {}", *connected_to_);

        WiFiBaseStation base_station{};
        base_station.target_channel = ap_info.primary;  // Set channel
        // Copy the BSSID to the configuration
        memcpy(base_station.target_bssid, ap_info.bssid,
               sizeof(base_station.target_bssid));
        std::array<uint8_t, sizeof(base_station.target_bssid)> debug_bssid;
        memcpy(debug_bssid.data(), base_station.target_bssid,
               sizeof(base_station.target_bssid));
        AE_TELED_DEBUG("Storing to cash BSSID:{} CHN:{}", debug_bssid,
                       static_cast<int>(base_station.target_channel));

        connection_state_.state = State::kConnected;
        event_task_sub_ = ae_context_.scheduler().Task(
            [&]() { connect_res_event_.Emit(Ok{base_station}); });
        break;
      }
      default:
        break;
    }
  }
}
void EspWifiDriver::ConnectedEventHandler(esp_event_base_t event_base,
                                          int32_t event_id, void* event_data) {
  AE_TELED_DEBUG("Wifi event on Connected");
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_DISCONNECTED: {
        auto* event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
        AE_TELED_DEBUG("Wifi event disconnected, reasone {}",
                       static_cast<int>(event->reason));
        break;
      }
      default:
        break;
    }
  }
  // TODO:
}

void EspWifiDriver::DisconnectingEventHandler(esp_event_base_t /* event_base */,
                                              int32_t /* event_id */,
                                              void* /* event_data */) {
  AE_TELED_DEBUG("Wifi event on Disconnecting");
  // TODO:
}

void EspWifiDriver::DisconnectedEventHandler(esp_event_base_t /* event_base */,
                                             int32_t /* event_id */,
                                             void* /* event_data */) {
  AE_TELED_DEBUG("Wifi event on Disconnected");
  // TODO:
}

}  // namespace ae
#endif
