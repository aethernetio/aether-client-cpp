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

#ifndef AETHER_ADAPTERS_ESP32_WIFI_H_
#define AETHER_ADAPTERS_ESP32_WIFI_H_

#if (defined(ESP_PLATFORM))

#  define ESP32_WIFI_ADAPTER_ENABLED 1

#  include <string>
#  include <memory>

#  include "freertos/FreeRTOS.h"
#  include "freertos/task.h"
#  include "freertos/event_groups.h"
#  include "esp_system.h"
#  include "esp_wifi.h"
#  include "esp_event.h"
#  include "esp_log.h"
#  include "nvs_flash.h"

#  include "lwip/err.h"
#  include "lwip/sys.h"

#  include "aether/events/events.h"
#  include "aether/events/event_subscription.h"

#  include "aether/adapters/parent_wifi.h"

namespace ae {
class Esp32WifiAdapter;
namespace esp32_wifi_internal {
class EspWifiTransportBuilderAction final : public TransportBuilderAction {
  enum class State : std::uint8_t {
    kWaitConnection,
    kAddressResolve,
    kBuildersCreate,
    kBuildersCreated,
    kFailed
  };

 public:
  // immediately create the transport
  EspWifiTransportBuilderAction(ActionContext action_context,
                                Esp32WifiAdapter& adapter,
                                UnifiedAddress address_port_protocol);
  // create the transport when wifi is connected
  EspWifiTransportBuilderAction(
      ActionContext action_context,
      EventSubscriber<void(bool)> wifi_connected_event,
      Esp32WifiAdapter& adapter, UnifiedAddress address_port_protocol);

  UpdateStatus Update() override;

  std::vector<std::unique_ptr<ITransportStreamBuilder>> builders() override;

 private:
  void ResolveAddress();
  void CreateBuilders();

  Esp32WifiAdapter* adapter_;
  UnifiedAddress address_port_protocol_;
  std::vector<IpAddressPortProtocol> ip_address_port_protocols_;
  std::vector<std::unique_ptr<ITransportStreamBuilder>> transport_builders_;
  StateMachine<State> state_;
  Subscription state_changed_;
  Subscription wifi_connected_subscription_;
  Subscription address_resolve_sub_;
};
}  // namespace esp32_wifi_internal

class Esp32WifiAdapter : public ParentWifiAdapter {
  AE_OBJECT(Esp32WifiAdapter, ParentWifiAdapter, 0)

  Esp32WifiAdapter() = default;

  static constexpr int kMaxRetry = 10;

 public:
#  ifdef AE_DISTILLATION
  Esp32WifiAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
                   DnsResolver::ptr dns_resolver, std::string ssid,
                   std::string pass, Domain* domain);
#  endif  // AE_DISTILLATION

  ~Esp32WifiAdapter() override;

  AE_OBJECT_REFLECT(AE_MMBRS(esp_netif_, connected_, wifi_connected_event_))

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_);
  }
  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_);
  }

  ActionPtr<TransportBuilderAction> CreateTransport(
      UnifiedAddress const& address_port_protocol) override;

  void Update(TimePoint p) override;

 private:
  void Connect(void);
  void DisConnect(void);

  static void EventHandler(void* arg, esp_event_base_t event_base,
                           int32_t event_id, void* event_data);
  void WifiInitSta(void);
  void WifiInitNvs(void);

  esp_netif_t* esp_netif_{};
  bool connected_{false};
  Event<void(bool result)> wifi_connected_event_;
};
}  // namespace ae

#endif  // (defined(ESP_PLATFORM))

#endif  // AETHER_ADAPTERS_ESP32_WIFI_H_
