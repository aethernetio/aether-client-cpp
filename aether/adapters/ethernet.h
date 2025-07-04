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

#ifndef AETHER_ADAPTERS_ETHERNET_H_
#define AETHER_ADAPTERS_ETHERNET_H_

#include <optional>

#include "aether/memory.h"
#include "aether/poller/poller.h"
#include "aether/obj/dummy_obj.h"    // IWYU pragma: keep
#include "aether/dns/dns_resolve.h"  // IWYU pragma: keep
#include "aether/adapters/adapter.h"
#include "aether/actions/action_list.h"
#include "aether/types/state_machine.h"
#include "aether/events/event_subscription.h"

namespace ae {
class Aether;
class EthernetAdapter;
namespace ethernet_adapter_internal {
class EthernetTransportBuilder;

class EthernetTransportBuilderAction final : public TransportBuilderAction {
 public:
  enum class State : std::uint8_t {
    kAddressResolve,
    kBuildersCreate,
    kBuildersCreated,
    kFailed
  };

  EthernetTransportBuilderAction(ActionContext action_context,
                                 EthernetAdapter& adapter,
                                 UnifiedAddress address_port_protocol);
  ActionResult Update() override;

  std::vector<std::unique_ptr<ITransportBuilder>> builders() override;

 private:
  void ResolveAddress();
  void CreateBuilders();

  EthernetAdapter* adapter_;
  UnifiedAddress address_port_protocol_;
  std::vector<IpAddressPortProtocol> ip_address_port_protocols_;
  std::vector<std::unique_ptr<ITransportBuilder>> transport_builders_;
  StateMachine<State> state_;
  Subscription state_changed_;
  Subscription address_resolved_;
  Subscription resolving_failed_;
};
}  // namespace ethernet_adapter_internal

/**
 * \brief The Ethernet interface adapter.
 * This is the most common and suitable for most devices adapter. It's not tied
 * to particular ethernet device, and does not require any additional setup.
 * It's just creates transport the most common way for your system (desktop or
 * mobile).
 */
class EthernetAdapter final : public Adapter {
  friend class ethernet_adapter_internal ::EthernetTransportBuilderAction;
  friend class ethernet_adapter_internal::EthernetTransportBuilder;

  AE_OBJECT(EthernetAdapter, Adapter, 0)

  EthernetAdapter() = default;

 public:
#ifdef AE_DISTILLATION
  EthernetAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
                  DnsResolver::ptr dns_resolver, Domain* domain);
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, poller_, dns_resolver_))

  ActionView<TransportBuilderAction> CreateTransport(
      UnifiedAddress const& address_port_protocol) override;

 private:
  Obj::ptr aether_;
  IPoller::ptr poller_;
  DnsResolver::ptr dns_resolver_;

  std::optional<
      ActionList<ethernet_adapter_internal::EthernetTransportBuilderAction>>
      create_transport_actions_;
};
}  // namespace ae

#endif  // AETHER_ADAPTERS_ETHERNET_H_
