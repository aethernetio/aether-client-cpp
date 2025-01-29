/*
 * Copyright 2024 Aethernet Inc.
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

#include "aether/adapters/esp32_wifi.h"

#if defined ESP32_WIFI_ADAPTER_ENABLED
#  include <string.h>

#  include <utility>
#  include <memory>

#  include "aether/aether.h"
#  include "aether/tele/tele.h"

#  include "aether/transport/low_level/tcp/lwip_tcp.h"

/* FreeRTOS event group to signal when we are connected */
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about
 * two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries
 */
#  define WIFI_CONNECTED_BIT BIT0
#  define WIFI_FAIL_BIT BIT1

static int s_retry_num{0};

namespace ae {

Esp32WifiAdapter::CreateTransportAction::CreateTransportAction(
    ActionContext action_context, Esp32WifiAdapter* adapter, Obj::ptr aether,
    IPoller::ptr poller, IpAddressPortProtocol address_port_protocol)
    : ae::CreateTransportAction{action_context},
      adapter_{adapter},
      aether_{std::move(aether)},
      poller_{std::move(poller)},
      address_port_protocol_{std::move(address_port_protocol)},
      once_{true},
      failed_{false} {
  AE_TELED_DEBUG("CreateTransport immediately!");
  CreateTransport();
}

Esp32WifiAdapter::CreateTransportAction::CreateTransportAction(
    ActionContext action_context,
    EventSubscriber<void(bool)> wifi_connected_event, Esp32WifiAdapter* adapter,
    Obj::ptr aether, IPoller::ptr poller,
    IpAddressPortProtocol address_port_protocol)
    : ae::CreateTransportAction{action_context},
      adapter_{adapter},
      aether_{std::move(aether)},
      poller_{std::move(poller)},
      address_port_protocol_{std::move(address_port_protocol)},
      once_{true},
      failed_{false},
      wifi_connected_subscription_{
          wifi_connected_event.Subscribe([this](auto result) {
            if (result) {
              CreateTransport();
            } else {
              failed_ = true;
            }
            Action::Trigger();
          })} {
  AE_TELED_DEBUG("CreateTransport wait for wifi connection!");
}

TimePoint Esp32WifiAdapter::CreateTransportAction::Update(
    TimePoint current_time) {
  if (transport_ && once_) {
    Action::Result(*this);
    once_ = false;
  } else if (failed_ && once_) {
    Action::Error(*this);
    once_ = false;
  }
  return current_time;
}

Ptr<ITransport> Esp32WifiAdapter::CreateTransportAction::transport() const {
  return transport_;
}

void Esp32WifiAdapter::CreateTransportAction::CreateTransport() {
  adapter_->CleanDeadTransports();
  transport_ = adapter_->FindInCache(address_port_protocol_);
  if (!transport_) {
#  if defined(LWIP_TCP_TRANSPORT_ENABLED)
    assert(address_port_protocol_.protocol == Protocol::kTcp);
    transport_ =
        MakePtr<LwipTcpTransport>(*aether_.as<Aether>()->action_processor,
                                  poller_, address_port_protocol_);
#  else
    return {};
#  endif
  } else {
    AE_TELED_DEBUG("Got transport from cache");
  }

  adapter_->AddToCache(address_port_protocol_, transport_);
}

#  if defined AE_DISTILLATION
Esp32WifiAdapter::Esp32WifiAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
                                   std::string ssid, std::string pass,
                                   Domain* domain)
    : ParentWifiAdapter{std::move(aether), std::move(poller), std::move(ssid),
                        std::move(pass), domain} {
  AE_TELED_DEBUG("Esp32Wifi instance created!");
}

Esp32WifiAdapter::~Esp32WifiAdapter() {
  if (connected_ == true) {
    DisConnect();
    AE_TELED_DEBUG("Esp32Wifi instance deleted!");
    connected_ = false;
  }
}
#  endif  // AE_DISTILLATION

ActionView<ae::CreateTransportAction> Esp32WifiAdapter::CreateTransport(
    IpAddressPortProtocol const& address_port_protocol) {
  if (!create_transport_actions_) {
    create_transport_actions_ = MakePtr<ActionList<CreateTransportAction>>(
        ActionContext{*aether_.as<Aether>()->action_processor});
  }
  if (connected_) {
    return create_transport_actions_->Emplace(this, aether_, poller_,
                                              address_port_protocol);
  } else {
    return create_transport_actions_->Emplace(
        EventSubscriber{wifi_connected_event_}, this, aether_, poller_,
        address_port_protocol);
  }
}

void Esp32WifiAdapter::Update(TimePoint t) {
  if (connected_ == false) {
    connected_ = true;
    Connect();
  }

  update_time_ = t;
}

void Esp32WifiAdapter::Connect(void) {
  if (esp_netif_ == nullptr) {
    WifiInitNvs();
    WifiInitSta();
  }
  AE_TELED_DEBUG("WiFi connected to the AP");
}

void Esp32WifiAdapter::DisConnect(void) {
  if (esp_netif_ != nullptr) {
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(esp_netif_);
  }
  AE_TELED_DEBUG("WiFi disconnected from the AP");
}

void Esp32WifiAdapter::EventHandler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data) {
  if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
    esp_wifi_connect();
  } else if ((event_base == WIFI_EVENT) &&
             (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
    if (s_retry_num < kMaxRetry) {
      esp_wifi_connect();
      s_retry_num++;
      AE_TELED_DEBUG("retry to connect to the AP");
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    AE_TELED_DEBUG("connect to the AP fail");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = static_cast<ip_event_got_ip_t*>(event_data);
    AE_TELED_DEBUG("got ip: {}.{}.{}.{}", IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void Esp32WifiAdapter::WifiInitSta(void) {
  int string_size{0};
  wifi_config_t wifi_config{};
  wifi_scan_threshold_t wifi_threshold{};

  s_wifi_event_group = xEventGroupCreate();

  // ESP_ERROR_CHECK(esp_netif_deinit());
  ESP_ERROR_CHECK(esp_netif_init());

  // ESP_ERROR_CHECK(esp_event_loop_delete_default());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_netif_ = esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &(Esp32WifiAdapter::EventHandler), nullptr,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &(Esp32WifiAdapter::EventHandler), nullptr,
      &instance_got_ip));

  wifi_threshold.rssi = 0;
  wifi_threshold.authmode = WIFI_AUTH_WPA2_PSK;

  wifi_config.sta.threshold = wifi_threshold;

  AE_TELED_DEBUG("Connecting to ap SSID:{} PSWD:{}", ssid_, pass_);
  std::fill_n(wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), 0);
  string_size = sizeof(wifi_config.sta.ssid);
  if (ssid_.size() < string_size) {
    string_size = ssid_.size();
  }
  ssid_.copy(reinterpret_cast<char*>(wifi_config.sta.ssid), string_size);

  std::fill_n(wifi_config.sta.password, sizeof(wifi_config.sta.password), 0);
  string_size = sizeof(wifi_config.sta.password);
  if (pass_.size() < string_size) {
    string_size = pass_.size();
  }
  pass_.copy(reinterpret_cast<char*>(wifi_config.sta.password), string_size);

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  AE_TELED_DEBUG("WifiInitSta finished.");

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or
   * connection failed for the maximum number of re-tries (WIFI_FAIL_BIT). The
   * bits are set by EventHandler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we
   * can test which event actually happened. */
  if (bits & WIFI_CONNECTED_BIT) {
    AE_TELED_DEBUG("Connected to ap SSID:{} PSWD:{}", wifi_config.sta.ssid,
                   wifi_config.sta.password);
    wifi_connected_event_.Emit(true);
  } else if (bits & WIFI_FAIL_BIT) {
    AE_TELED_DEBUG("Failed to connect to SSID:{}, PSWD:{}",
                   wifi_config.sta.ssid, wifi_config.sta.password);
    wifi_connected_event_.Emit(false);
  } else {
    AE_TELED_DEBUG("UNEXPECTED EVENT");
  }

  /* The event will not be processed after unregister */
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
  vEventGroupDelete(s_wifi_event_group);
}

void Esp32WifiAdapter::WifiInitNvs(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
}
}  // namespace ae
#endif  // defined ESP32_WIFI_ADAPTER_ENABLED
