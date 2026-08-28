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
#  include <inttypes.h>

#  include "esp_event.h"
#  include "esp_log.h"
#  include "esp_mac.h"
#  include "esp_private/wifi.h"
#  include "esp_system.h"
#  include "esp_wifi.h"
#  include "nvs_flash.h"

#  include "lwip/err.h"
#  include "lwip/ip4_addr.h"
#  include "lwip/ip6_addr.h"
#  include "lwip/sys.h"

#  include "aether/ae_exp_diag.h"
#  include "aether/ae_exp_wifi.h"
#  include "aether/tele.h"

extern "C" esp_err_t esp_wifi_internal_set_retry_counter(uint8_t short_retry,
                                                         uint8_t long_retry);
extern "C" esp_err_t esp_wifi_internal_get_fix_rate(wifi_interface_t ifx,
                                                    bool* is_fixed,
                                                    wifi_phy_rate_t* rate);

// Experiment hooks (esp32c6_aether_full_cycle); weak no-ops if undefined.
extern "C" void ae_exp_on_wifi_connect(int has_cached_bs) __attribute__((weak));
extern "C" void ae_exp_on_wifi_start(void) __attribute__((weak));
extern "C" void ae_exp_on_wifi_sta_start(void) __attribute__((weak));
extern "C" void ae_exp_on_wifi_sta_connected(int channel) __attribute__((weak));
extern "C" void ae_exp_on_wifi_got_ip(void) __attribute__((weak));
extern "C" void ae_exp_on_wifi_sta_disconnected(int reason)
    __attribute__((weak));

namespace ae {
#  define WIFI_CONNECTED_BIT BIT0
#  define WIFI_FAIL_BIT BIT1

namespace esp_wifi_driver_internal {
static constexpr char kTag[] = "EspWifiDriver";
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

  ESP_LOGI(kTag, "Event handler event_base %s event_id %" PRId32,
           base_type(event_base), event_id);

  auto* driver = static_cast<EspWifiDriver*>(arg);

  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_START:
        AE_EXP_LC("EspWifiDriver", driver->ExpId(), driver, 0, "STA_START", "");
        if (ae_exp_on_wifi_sta_start) {
          ae_exp_on_wifi_sta_start();
        }
        break;
      case WIFI_EVENT_STA_CONNECTED: {
        auto* ev = static_cast<wifi_event_sta_connected_t*>(event_data);
        AE_EXP_LC("EspWifiDriver", driver->ExpId(), driver, 0, "STA_CONNECTED",
                  "ch=%d", ev != nullptr ? static_cast<int>(ev->channel) : -1);
        if (ae_exp_on_wifi_sta_connected) {
          ae_exp_on_wifi_sta_connected(ev != nullptr ? ev->channel : 0);
        }
        break;
      }
      case WIFI_EVENT_STA_DISCONNECTED: {
        auto* event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
        AE_EXP_LC("EspWifiDriver", driver->ExpId(), driver, 0,
                  "STA_DISCONNECTED", "reason=%d",
                  event != nullptr ? static_cast<int>(event->reason) : -1);
        if (ae_exp_on_wifi_sta_disconnected) {
          ae_exp_on_wifi_sta_disconnected(
              event != nullptr ? static_cast<int>(event->reason) : -1);
        }
        break;
      }
      default:
        break;
    }
  } else if (event_base == IP_EVENT) {
    if (event_id == IP_EVENT_STA_GOT_IP || event_id == IP_EVENT_GOT_IP6) {
      AE_EXP_LC("EspWifiDriver", driver->ExpId(), driver, 0, "GOT_IP", "");
      if (ae_exp_on_wifi_got_ip) {
        ae_exp_on_wifi_got_ip();
      }
    }
  }

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

void SetupCredentials(wifi_config_t& wifi_config, WifiCreds const& creds) {
#  ifdef DEBUG
  // for debug purpose only, it's private data
  ESP_LOGD(kTag, "Connecting to ap SSID:%s PSWD:%s", creds.ssid.c_str(),
           creds.password.c_str());
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
    ESP_LOGE(kTag, "Failed to stop DHCP client: %s", esp_err_to_name(err));
    return err;
  }

#  if AE_SUPPORT_IPV4 == 1
  // Setting a static IP
  err = esp_netif_set_ip_info(netif, &ip_info);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "Failed to set IP info: %s", esp_err_to_name(err));
    return err;
  }

  // Installing DNS servers
  if (config.primary_dns_v4) {
    err = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info1);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "Failed to set primary DNS: %s", esp_err_to_name(err));
      return err;
    }
  }

  if (config.secondary_dns_v4) {
    err = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns_info2);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "Failed to set secondary DNS: %s", esp_err_to_name(err));
      return err;
    }
  }

  ESP_LOGD(kTag, "Static IP V4 configured: %u.%u.%u.%u",
           static_cast<unsigned>(config.static_ip_v4.ipv4_value[0]),
           static_cast<unsigned>(config.static_ip_v4.ipv4_value[1]),
           static_cast<unsigned>(config.static_ip_v4.ipv4_value[2]),
           static_cast<unsigned>(config.static_ip_v4.ipv4_value[3]));
#  endif
#  if AE_SUPPORT_IPV6 == 1
  if (config.static_ip_v6.has_value()) {
    err = esp_netif_set_ip6_global(netif, &ip_info_v6.ip);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "Failed to set IP V6 info: %s", esp_err_to_name(err));
      return err;
    }

    auto const& ip = config.static_ip_v6->ipv6_value;
    ESP_LOGD(kTag,
             "Static IP V6 configured: "
             "%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
             "%02x%02x:%02x%02x:%02x%02x:%02x%02x",
             static_cast<unsigned>(ip[0]), static_cast<unsigned>(ip[1]),
             static_cast<unsigned>(ip[2]), static_cast<unsigned>(ip[3]),
             static_cast<unsigned>(ip[4]), static_cast<unsigned>(ip[5]),
             static_cast<unsigned>(ip[6]), static_cast<unsigned>(ip[7]),
             static_cast<unsigned>(ip[8]), static_cast<unsigned>(ip[9]),
             static_cast<unsigned>(ip[10]), static_cast<unsigned>(ip[11]),
             static_cast<unsigned>(ip[12]), static_cast<unsigned>(ip[13]),
             static_cast<unsigned>(ip[14]), static_cast<unsigned>(ip[15]));
  }
#  endif

  return ESP_OK;
}

esp_err_t StartWifiConnection(
    EspWifiDriver* driver, esp_netif_t* espt_init_sta, WiFiAp const& wifi_ap,
    std::optional<WiFiPowerSaveParam> const& psp) {
  esp_err_t err = ESP_OK;

  wifi_config_t wifi_config{};

#  if AE_WIFI_USE_SCAN_THRESHOLD
  wifi_scan_threshold_t wifi_threshold{};
  wifi_threshold.rssi = 0;
  wifi_threshold.authmode = WIFI_AUTH_WPA2_PSK;

  wifi_config.sta.threshold = wifi_threshold;
  if (psp) {
    wifi_config.sta.listen_interval = psp->listen_interval;
  }
#  endif

  esp_wifi_driver_internal::SetupCredentials(wifi_config, wifi_ap.creds);

  // Setting up a static IP, if required
  if (wifi_ap.static_ip.has_value()) {
    err = esp_wifi_driver_internal::SetStaticIp(espt_init_sta,
                                                wifi_ap.static_ip.value());
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "Failed to set static IP, falling back to DHCP.");
      // If an error occurs, switch to DHCP
      return err;
    }
  } else {
    ESP_LOGD(kTag, "Using DHCP for IP configuration");
  }

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "Failed to set mode.");
    // If an error occurs, exit
    return err;
  }
  err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "Failed to set config.");
    // If an error occurs, exit
    return err;
  }
  if (psp) {
    err = esp_wifi_set_ps(static_cast<wifi_ps_type_t>(psp->wifi_ps_type));
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "Failed to set ps.");
      // If an error occurs, exit
      return err;
    }
    err = esp_wifi_set_protocol(WIFI_IF_STA, psp->protocol_bitmap);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "Failed to set protocol.");
      // If an error occurs, exit
      return err;
    }
  }

  if (ae_exp_on_wifi_start) {
    ae_exp_on_wifi_start();
  }
  AE_EXP_LC("EspWifiDriver", driver != nullptr ? driver->ExpId() : 0, driver, 0,
            "esp_wifi_start", "");
  err = esp_wifi_start();
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "Failed to start WiFi!");
    // If an error occurs, exit
    return err;
  }

  if (psp) {
    err = esp_wifi_internal_set_fix_rate(
        WIFI_IF_STA, true, static_cast<wifi_phy_rate_t>(psp->fix_rate));
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "Failed to set fix rate.");
      // If an error occurs, exit
      return err;
    }
    err =
        esp_wifi_internal_set_retry_counter(psp->short_retry, psp->long_retry);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "Failed to set retry counter.");
      // If an error occurs, exit
      return err;
    }
    err = esp_wifi_set_max_tx_power(psp->power);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "Failed to set tx power.");
      // If an error occurs, exit
      return err;
    }
  }

  ESP_LOGD(kTag, "WifiInitSta finished.");

  return err;
}

}  // namespace esp_wifi_driver_internal

EspWifiDriver::EspWifiDriver(AeContext const& ae_context)
    : ae_context_{ae_context}
#  if defined(AE_EXP_DIAG)
      ,
      exp_id_{AeExpNextId()}
#  endif
{
  AE_EXP_LC("EspWifiDriver", ExpId(), this, 0, "ctor_begin", "");
  esp_log_level_set(esp_wifi_driver_internal::kTag, ESP_LOG_DEBUG);
  Init();
  AE_EXP_LC("EspWifiDriver", ExpId(), this, 0, "ctor_end", "");
}

EspWifiDriver::~EspWifiDriver() {
  AE_EXP_LC("EspWifiDriver", ExpId(), this, 0, "dtor_begin", "");
  if (connected_to_) {
    Disconnect();
  }
  Deinit();
  AE_EXP_LC("EspWifiDriver", ExpId(), this, 0, "dtor_end", "");
}

void EspWifiDriver::Connect(WiFiAp const& wifi_ap,
                            std::optional<WiFiPowerSaveParam> const& psp) {
  bool const has_connected_to = connected_to_.has_value();
  bool const will_disconnect_first = has_connected_to;
  AE_EXP_LC("EspWifiDriver", ExpId(), this, 0, "Connect",
            "ssid=%s has_connected_to=%d will_disconnect_first=%d",
            wifi_ap.creds.ssid.c_str(), has_connected_to ? 1 : 0,
            will_disconnect_first ? 1 : 0);

  if (ae_exp_on_wifi_connect) {
    ae_exp_on_wifi_connect(0);
  }

  if (connected_to_) {
    AE_EXP_LC("EspWifiDriver", ExpId(), this, 0, "connect_forces_disconnect",
              "");
    Disconnect();
  }
  connected_to_.reset();

  connection_state_ = {};
  connection_state_.state = State::kConnecting;

  auto err = esp_wifi_driver_internal::StartWifiConnection(
      this, static_cast<esp_netif_t*>(espt_init_sta_), wifi_ap, psp);
  // the connection result will be handled in ConnectingEventHandler
  if (err != ESP_OK) {
    // Emitting the error, 2 for example.
    connect_res_event_.Emit(Error(2));
  }
}

EspWifiDriver::ConnectResEvent::Subscriber EspWifiDriver::connect_res_event() {
  return EventSubscriber{connect_res_event_};
}

std::optional<std::string> EspWifiDriver::connected_to() const {
  return connected_to_;
}

void EspWifiDriver::Init() {
  AE_EXP_LC("EspWifiDriver", ExpId(), this, 0, "Init_begin", "");
  esp_err_t err = ESP_OK;

  InitNvs();

  err = esp_netif_init();
  if (err != ESP_OK) {
    ESP_LOGE(esp_wifi_driver_internal::kTag, "Failed to netif init.");
    // If an error occurs, exit
    AE_EXP_LC("EspWifiDriver", ExpId(), this, 0, "Init_end", "err=netif_init");
    return;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK) {
    ESP_LOGE(esp_wifi_driver_internal::kTag, "Failed to create event loop.");
    // If an error occurs, exit
    AE_EXP_LC("EspWifiDriver", ExpId(), this, 0, "Init_end", "err=event_loop");
    return;
  }
  event_loop_created_ = true;

  espt_init_sta_ = esp_netif_create_default_wifi_sta();

  wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
#  if AE_WIFI_USE_AMPDU_OFF
  // We disable aggregation so that the packages go out one by one and quickly
  wifi_init_config.ampdu_rx_enable = 0;
  wifi_init_config.ampdu_tx_enable = 0;
#  endif

  err = esp_wifi_init(&wifi_init_config);
  if (err != ESP_OK) {
    ESP_LOGE(esp_wifi_driver_internal::kTag, "Failed to wifi init.");
    // If an error occurs, exit
    AE_EXP_LC("EspWifiDriver", ExpId(), this, 0, "Init_end", "err=wifi_init");
    return;
  }

  RegisterEventHandlers();
  connection_state_ = {};
  connection_state_.state = State::kDisconnected;
  AE_EXP_LC("EspWifiDriver", ExpId(), this, 0, "Init_end", "ok");
}

void EspWifiDriver::RegisterEventHandlers() {
#  if AE_WIFI_USE_INSTANCE_HANDLERS
  esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, esp_wifi_driver_internal::EventHandler, this,
      &wifi_handler_inst_);
  esp_event_handler_instance_register(
      IP_EVENT, ESP_EVENT_ANY_ID, esp_wifi_driver_internal::EventHandler, this,
      &ip_handler_inst_);
#  else
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                             esp_wifi_driver_internal::EventHandler, this);
  esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                             esp_wifi_driver_internal::EventHandler, this);
#  endif
}

void EspWifiDriver::UnregisterEventHandlers() {
#  if AE_WIFI_USE_INSTANCE_HANDLERS
  if (wifi_handler_inst_ != nullptr) {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          wifi_handler_inst_);
    wifi_handler_inst_ = nullptr;
  }
  if (ip_handler_inst_ != nullptr) {
    esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                                          ip_handler_inst_);
    ip_handler_inst_ = nullptr;
  }
#  else
  esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               esp_wifi_driver_internal::EventHandler);
  esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                               esp_wifi_driver_internal::EventHandler);
#  endif
}

void EspWifiDriver::InitNvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    err = nvs_flash_erase();
    if (err != ESP_OK) {
      ESP_LOGE(esp_wifi_driver_internal::kTag, "Failed to flash erase.");
      return;
    }
    err = nvs_flash_init();
    if (err != ESP_OK) {
      ESP_LOGE(esp_wifi_driver_internal::kTag, "Failed to flash init.");
      return;
    }
  }
}

void EspWifiDriver::Deinit() {
  AE_EXP_LC("EspWifiDriver", ExpId(), this, 0, "Deinit", "");
#  if AE_WIFI_USE_FULL_DEINIT
  esp_wifi_disconnect();
  esp_wifi_stop();
  UnregisterEventHandlers();
  esp_wifi_deinit();
  if (espt_init_sta_ != nullptr) {
    esp_netif_destroy_default_wifi(static_cast<esp_netif_t*>(espt_init_sta_));
    espt_init_sta_ = nullptr;
  }
  if (event_loop_created_) {
    esp_event_loop_delete_default();
    event_loop_created_ = false;
  }
#  else
  // Stop before deinit; otherwise a subsequent Construct can see a half-live
  // wifi stack (esp_wifi_init → already initialized) on ESP-IDF v6.
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_netif_destroy_default_wifi(static_cast<esp_netif_t*>(espt_init_sta_));
  espt_init_sta_ = nullptr;

  UnregisterEventHandlers();
#  endif
}

void EspWifiDriver::Disconnect() {
  AE_EXP_LC("EspWifiDriver", ExpId(), this, 0, "Disconnect", "");
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
        ESP_LOGD(esp_wifi_driver_internal::kTag,
                 "Wifi event disconnected, reason %d",
                 static_cast<int>(event->reason));
#  if AE_WIFI_USE_CONNECT_RETRY10
        if (connection_state_.retry_count <
            esp_wifi_driver_internal::kMaxRetry) {
          esp_wifi_connect();
          connection_state_.retry_count++;
        } else
#  endif
        {
#  if AE_WIFI_USE_FAIL_DISCONNECT
          event_task_sub_ = ae_context_.scheduler().Task([&]() {
            Disconnect();
            // connection failed
            connect_res_event_.Emit(Error(1));
          });
#  else
          connect_res_event_.Emit(Error(1));
#  endif
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
        wifi_ap_record_t ap_info{};
        esp_wifi_sta_get_ap_info(&ap_info);
        connected_to_ = std::string(reinterpret_cast<char*>(ap_info.ssid));
        ESP_LOGD(esp_wifi_driver_internal::kTag, "Connected to AP %s",
                 connected_to_->c_str());

        connection_state_.state = State::kConnected;
        event_task_sub_ = ae_context_.scheduler().Task([&]() {
          connect_res_event_.Emit(Ok{ConnectOk{}});
        });
        break;
      }
      default:
        break;
    }
  }
}
void EspWifiDriver::ConnectedEventHandler(esp_event_base_t event_base,
                                          int32_t event_id, void* event_data) {
  ESP_LOGD(esp_wifi_driver_internal::kTag, "Wifi event on Connected");
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_DISCONNECTED: {
        auto* event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
        ESP_LOGD(esp_wifi_driver_internal::kTag,
                 "Wifi event disconnected, reason %d",
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
  ESP_LOGD(esp_wifi_driver_internal::kTag, "Wifi event on Disconnecting");
  // TODO:
}

void EspWifiDriver::DisconnectedEventHandler(esp_event_base_t /* event_base */,
                                             int32_t /* event_id */,
                                             void* /* event_data */) {
  ESP_LOGD(esp_wifi_driver_internal::kTag, "Wifi event on Disconnected");
  // TODO:
}

}  // namespace ae
#endif
