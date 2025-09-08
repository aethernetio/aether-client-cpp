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
#  include <cassert>

#  include "aether/memory.h"
#  include "aether/aether.h"
#  include "aether/adapters/adapter_tele.h"

#  include "aether/transport/low_level/tcp/tcp.h"
#  include "aether/transport/low_level/udp/udp.h"

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
namespace esp32_wifi_internal {
class EspWifiTransportBuilder final : public ITransportStreamBuilder {
 public:
  EspWifiTransportBuilder(Esp32WifiAdapter& adapter,
                          IpAddressPortProtocol address_port_protocol)
      : adapter_{&adapter},
        address_port_protocol_{std::move(address_port_protocol)} {}

  std::unique_ptr<ByteIStream> BuildTransportStream() override {
    switch (address_port_protocol_.protocol) {
      case Protocol::kTcp:
        return BuildTcp();
      case Protocol::kUdp:
        return BuildUdp();
      default:
        assert(false);
        return nullptr;
    }
  }

 private:
  std::unique_ptr<ByteIStream> BuildTcp() {
#  if defined COMMON_TCP_TRANSPORT_ENABLED
    assert(address_port_protocol_.protocol == Protocol::kTcp);
    return make_unique<TcpTransport>(*adapter_->aether_.as<Aether>(),
                                     adapter_->poller_, address_port_protocol_);
#  else
    static_assert(false, "No transport enabled");
#  endif
  }

  std::unique_ptr<ByteIStream> BuildUdp() {
#  if defined COMMON_UDP_TRANSPORT_ENABLED
    assert(address_port_protocol_.protocol == Protocol::kUdp);
    return make_unique<UdpTransport>(*adapter_->aether_.as<Aether>(),
                                     adapter_->poller_, address_port_protocol_);
#  else
    static_assert(false, "No transport enabled");
#  endif
  }

  Esp32WifiAdapter* adapter_;
  IpAddressPortProtocol address_port_protocol_;
};

EspWifiTransportBuilderAction::EspWifiTransportBuilderAction(
    ActionContext action_context, Esp32WifiAdapter& adapter,
    UnifiedAddress address_port_protocol)
    : TransportBuilderAction{action_context},
      adapter_{&adapter},
      address_port_protocol_{std::move(address_port_protocol)},
      state_{State::kAddressResolve},
      state_changed_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELE_DEBUG(kEspWifiAdapterWifiTransportImmediately,
                "Wifi connected, create transport immediately");
}

EspWifiTransportBuilderAction::EspWifiTransportBuilderAction(
    ActionContext action_context,
    EventSubscriber<void(bool)> wifi_connected_event, Esp32WifiAdapter& adapter,
    UnifiedAddress address_port_protocol)
    : TransportBuilderAction{action_context},
      adapter_{&adapter},
      address_port_protocol_{std::move(address_port_protocol)},
      state_{State::kWaitConnection},
      wifi_connected_subscription_{
          wifi_connected_event.Subscribe([this](auto result) {
            if (result) {
              state_ = State::kAddressResolve;
            } else {
              state_ = State::kFailed;
            }
          })} {
  AE_TELE_DEBUG(kEspWifiAdapterWifiTransportWait, "Wait wifi connection");
}

UpdateStatus EspWifiTransportBuilderAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaitConnection:
        break;
      case State::kAddressResolve:
        ResolveAddress();
        break;
      case State::kBuildersCreate:
        CreateBuilders();
        break;
      case State::kBuildersCreated:
        return UpdateStatus::Result();
      case State::kFailed:
        return UpdateStatus::Error();
    }
  }
  return {};
}

std::vector<std::unique_ptr<ITransportStreamBuilder>>
EspWifiTransportBuilderAction::builders() {
  return std::move(transport_builders_);
}

#  if AE_SUPPORT_CLOUD_DNS
void EspWifiTransportBuilderAction::ResolveAddress() {
  std::visit(reflect::OverrideFunc{
                 [this](IpAddressPortProtocol const& ip_address) {
                   ip_address_port_protocols_.push_back(ip_address);
                   state_ = State::kBuildersCreate;
                 },
                 [this](NameAddress const& name_address) {
                   auto dns_resolver = adapter_->dns_resolver_;
                   assert(dns_resolver);
                   auto resolve_action = dns_resolver->Resolve(name_address);

                   address_resolve_sub_ =
                       resolve_action->StatusEvent().Subscribe(ActionHandler{
                           OnResult{[this](auto& action) {
                             ip_address_port_protocols_ =
                                 std::move(action.addresses);
                             state_ = State::kBuildersCreate;
                           }},
                           OnError{[this]() { state_ = State::kFailed; }}});
                 }},
             address_port_protocol_);
}
#  else
void EspWifiTransportBuilderAction::ResolveAddress() {
  auto ip_address = std::get<IpAddressPortProtocol>(address_port_protocol_);
  ip_address_port_protocols_.push_back(ip_address);
  state_ = State::kBuildersCreate;
}
#  endif

void EspWifiTransportBuilderAction::CreateBuilders() {
  for (auto const& ip_address_port_protocol : ip_address_port_protocols_) {
    auto builder = std::make_unique<EspWifiTransportBuilder>(
        *adapter_, ip_address_port_protocol);
    transport_builders_.push_back(std::move(builder));
  }
  state_ = State::kBuildersCreated;
}
}  // namespace esp32_wifi_internal

#  if defined AE_DISTILLATION
Esp32WifiAdapter::Esp32WifiAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
                                   DnsResolver::ptr dns_resolver,
                                   std::string ssid, std::string pass,
                                   Domain* domain)
    : ParentWifiAdapter{std::move(aether),       std::move(poller),
                        std::move(dns_resolver), std::move(ssid),
                        std::move(pass),         domain} {
  AE_TELED_DEBUG("Esp32Wifi instance created!");
}
#  endif  // AE_DISTILLATION

Esp32WifiAdapter::~Esp32WifiAdapter() {
  if (connected_ == true) {
    DisConnect();
    AE_TELED_DEBUG("Esp32Wifi instance deleted!");
    connected_ = false;
  }
}

ActionPtr<TransportBuilderAction> Esp32WifiAdapter::CreateTransport(
    UnifiedAddress const& address_port_protocol) {
  AE_TELE_INFO(kEspWifiAdapterCreate, "Create transport for {}",
               address_port_protocol);
  if (connected_) {
    return ActionPtr<esp32_wifi_internal::EspWifiTransportBuilderAction>{
        *aether_.as<Aether>(), *this, address_port_protocol};
  } else {
    return ActionPtr<esp32_wifi_internal::EspWifiTransportBuilderAction>{
        *aether_.as<Aether>(), EventSubscriber{wifi_connected_event_}, *this,
        address_port_protocol};
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
}

void Esp32WifiAdapter::DisConnect(void) {
  if (esp_netif_ != nullptr) {
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(esp_netif_);
  }
  AE_TELE_DEBUG(kEspWifiAdapterWifiDisconnected,
                "WiFi disconnected from the AP");
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
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
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

  // for debug purpose only, it's private data
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

  AE_TELE_DEBUG(kEspWifiAdapterWifiInitiated, "WifiInitSta finished.");

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or
   * connection failed for the maximum number of re-tries (WIFI_FAIL_BIT). The
   * bits are set by EventHandler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we
   * can test which event actually happened. */
  if (bits & WIFI_CONNECTED_BIT) {
    AE_TELE_DEBUG(kEspWifiAdapterWifiEventConnected, "Connected to AP");
    wifi_connected_event_.Emit(true);
  } else if (bits & WIFI_FAIL_BIT) {
    AE_TELE_DEBUG(kEspWifiAdapterWifiEventDisconnected,
                  "Failed to connect to AP");
    wifi_connected_event_.Emit(false);
  } else {
    AE_TELE_DEBUG(kEspWifiAdapterWifiEventUnexpected, "UNEXPECTED EVENT {}",
                  static_cast<int>(bits));
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
