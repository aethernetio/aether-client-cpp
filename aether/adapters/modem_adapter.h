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

#ifndef AETHER_ADAPTERS_MODEM_ADAPTER_H_
#define AETHER_ADAPTERS_MODEM_ADAPTER_H_

#define MODEM_ADAPTER_ENABLED 1

#include <cstdint>

#include "aether/adapters/parent_modem.h"
#include "aether/modems/imodem_driver.h"

#include "aether/events/events.h"
#include "aether/actions/action_ptr.h"
#include "aether/events/event_subscription.h"

#define MODEM_TCP_TRANSPORT_ENABLED 1

namespace ae {
class ModemAdapter;
namespace modem_adapter_internal {
class ModemAdapterTransportBuilderAction final : public TransportBuilderAction {
  enum class State : std::uint8_t {
    kWaitConnection,
    kAddressResolve,
    kBuildersCreate,
    kBuildersCreated,
    kFailed
  };

 public:
  // immediately create the transport
  ModemAdapterTransportBuilderAction(ActionContext action_context,
                                     ModemAdapter& adapter,
                                     UnifiedAddress address_port_protocol);
  // create the transport when lte modem is connected
  ModemAdapterTransportBuilderAction(
      ActionContext action_context,
      EventSubscriber<void(bool)> lte_modem_connected_event,
      ModemAdapter& adapter, UnifiedAddress address_port_protocol);

  UpdateStatus Update() override;

  std::vector<std::unique_ptr<ITransportBuilder>> builders() override;

 private:
  void ResolveAddress();
  void CreateBuilders();

  ModemAdapter* adapter_;
  UnifiedAddress address_port_protocol_;
  std::vector<IpAddressPortProtocol> ip_address_port_protocols_;
  std::vector<std::unique_ptr<ITransportBuilder>> transport_builders_;
  StateMachine<State> state_;
  Subscription state_changed_;
  Subscription lte_modem_connected_subscription_;
  Subscription resolve_sub_;
};
}  // namespace modem_adapter_internal

class ModemAdapter : public ParentModemAdapter {
  AE_OBJECT(ModemAdapter, ParentModemAdapter, 0)

  ModemAdapter() = default;

 public:
#ifdef AE_DISTILLATION
  ModemAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
               DnsResolver::ptr dns_resolver, ModemInit modem_init,
               Domain* domain);
#endif  // AE_DISTILLATION

  ~ModemAdapter() override;

  AE_OBJECT_REFLECT(AE_MMBRS(modem_connected_event_, modem_driver_))

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

  void Update(TimePoint current_time) override;

 private:
  void Connect();
  void DisConnect();

  bool connected_{false};
  Event<void(bool result)> modem_connected_event_;
  std::unique_ptr<IModemDriver> modem_driver_;
};

}  // namespace ae

#endif  // AETHER_ADAPTERS_MODEM_ADAPTER_H_
