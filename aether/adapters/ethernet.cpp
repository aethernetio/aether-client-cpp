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

#include "aether/adapters/ethernet.h"

#include <utility>

#include "aether/aether.h"
#include "aether/reflect/reflect.h"
#include "aether/adapters/adapter_tele.h"

// IWYU pragma: begin_keeps
#include "aether/transport/low_level/tcp/tcp.h"
#include "aether/transport/low_level/udp/udp.h"
// IWYU pragma: end_keeps

namespace ae {
namespace ethernet_adapter_internal {
class EthernetTransportBuilder final : public ITransportStreamBuilder {
 public:
  EthernetTransportBuilder(EthernetAdapter& adapter,
                           IpAddressPortProtocol address_port_protocol)
      : adapter_{&adapter},
        address_port_protocol_{std::move(address_port_protocol)} {}

  ~EthernetTransportBuilder() override = default;

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
#if defined COMMON_TCP_TRANSPORT_ENABLED
    assert(address_port_protocol_.protocol == Protocol::kTcp);
    return make_unique<TcpTransport>(*adapter_->aether_.as<Aether>(),
                                     adapter_->poller_, address_port_protocol_);
#else
    static_assert(false, "No transport enabled");
#endif
  }

  std::unique_ptr<ByteIStream> BuildUdp() {
#if defined COMMON_UDP_TRANSPORT_ENABLED
    assert(address_port_protocol_.protocol == Protocol::kUdp);
    return make_unique<UdpTransport>(*adapter_->aether_.as<Aether>(),
                                     adapter_->poller_, address_port_protocol_);
#else
    static_assert(false, "No transport enabled");
#endif
  }

  EthernetAdapter* adapter_;
  IpAddressPortProtocol address_port_protocol_;
};

EthernetTransportBuilderAction::EthernetTransportBuilderAction(
    ActionContext action_context, class EthernetAdapter& adapter,
    UnifiedAddress address_port_protocol)
    : TransportBuilderAction{action_context},
      adapter_{&adapter},
      address_port_protocol_{std::move(address_port_protocol)},
      // get state based on the type of address is used
      state_{State::kAddressResolve},
      state_changed_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {}

UpdateStatus EthernetTransportBuilderAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
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
EthernetTransportBuilderAction::builders() {
  return std::move(transport_builders_);
}

#if AE_SUPPORT_CLOUD_DNS
void EthernetTransportBuilderAction::ResolveAddress() {
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
#else
void EthernetTransportBuilderAction::ResolveAddress() {
  auto ip_address = std::get<IpAddressPortProtocol>(address_port_protocol_);
  ip_address_port_protocols_.push_back(ip_address);
  state_ = State::kBuildersCreate;
}
#endif

void EthernetTransportBuilderAction::CreateBuilders() {
  for (auto const& ip_address_port_protocol : ip_address_port_protocols_) {
    auto builder = std::make_unique<EthernetTransportBuilder>(
        *adapter_, ip_address_port_protocol);
    transport_builders_.push_back(std::move(builder));
  }
  state_ = State::kBuildersCreated;
}

}  // namespace ethernet_adapter_internal
#ifdef AE_DISTILLATION
EthernetAdapter::EthernetAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
                                 DnsResolver::ptr dns_resolver, Domain* domain)
    : Adapter{domain},
      aether_{std::move(aether)},
      poller_{std::move(poller)},
      dns_resolver_{std::move(dns_resolver)} {
  AE_TELED_INFO("EthernetAdapter created");
}
#endif  // AE_DISTILLATION

ActionPtr<TransportBuilderAction> EthernetAdapter::CreateTransport(
    UnifiedAddress const& address_port_protocol) {
  AE_TELE_INFO(kEthernetAdapterCreate, "Create transport for {}",
               address_port_protocol);

  return ActionPtr<ethernet_adapter_internal::EthernetTransportBuilderAction>{
      *aether_.as<Aether>(), *this, address_port_protocol};
}

}  // namespace ae
