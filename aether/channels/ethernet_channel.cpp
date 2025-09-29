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

#include "aether/channels/ethernet_channel.h"

#include "aether/memory.h"
#include "aether/actions/action.h"
#include "aether/types/state_machine.h"
#include "aether/events/event_subscription.h"

#include "aether/aether.h"
#include "aether/poller/poller.h"
#include "aether/dns/dns_resolve.h"

// IWYU pragma: begin_keeps
#include "aether/transport/low_level/tcp/tcp.h"
#include "aether/transport/low_level/udp/udp.h"
// IWYU pragma: end_keeps

namespace ae {
namespace ethernet_access_point_internal {
class EthernetTransportBuilder final : public ITransportStreamBuilder {
 public:
  EthernetTransportBuilder(ActionContext action_context,
                           IPoller::ptr const& poller,
                           IpAddressPortProtocol address_port_protocol)
      : action_context_{action_context},
        poller_{poller},
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
#if defined COMMON_TCP_TRANSPORT_ENABLED
    IPoller::ptr poller = poller_.Lock();
    return make_unique<TcpTransport>(action_context_, poller,
                                     address_port_protocol_);
#else
    static_assert(false, "No transport enabled");
#endif
  }

  std::unique_ptr<ByteIStream> BuildUdp() {
#if defined COMMON_UDP_TRANSPORT_ENABLED
    IPoller::ptr poller = poller_.Lock();
    return make_unique<UdpTransport>(action_context_, poller,
                                     address_port_protocol_);
#else
    static_assert(false, "No transport enabled");
#endif
  }

  ActionContext action_context_;
  PtrView<IPoller> poller_;
  IpAddressPortProtocol address_port_protocol_;
};

class EthernetTransportBuilderAction final : public TransportBuilderAction {
 public:
  enum class State : std::uint8_t {
    kAddressResolve,
    kBuildersCreate,
    kBuildersCreated,
    kFailed
  };

  EthernetTransportBuilderAction(ActionContext action_context,
                                 UnifiedAddress address,
                                 DnsResolver::ptr const& dns_resolver,
                                 IPoller::ptr const& poller)
      : TransportBuilderAction{action_context},
        action_context_{action_context},
        address_{std::move(address)},
        resolver_{dns_resolver},
        poller_{poller},
        state_{State::kAddressResolve} {}

  UpdateStatus Update() override {
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

  std::vector<std::unique_ptr<ITransportStreamBuilder>> builders() override;

 private:
#if AE_SUPPORT_CLOUD_DNS
  void ResolveAddress() {
    std::visit(reflect::OverrideFunc{
                   [this](IpAddressPortProtocol const& ip_address) {
                     ip_addresses_.push_back(ip_address);
                     state_ = State::kBuildersCreate;
                   },
                   [this](NameAddress const& name_address) {
                     auto dns_resolver = resolver_.Lock();
                     assert(dns_resolver);
                     auto resolve_action = dns_resolver->Resolve(name_address);

                     address_resolve_sub_ =
                         resolve_action->StatusEvent().Subscribe(ActionHandler{
                             OnResult{[this](auto& action) {
                               ip_addresses_ = std::move(action.addresses);
                               state_ = State::kBuildersCreate;
                             }},
                             OnError {
                               [this]() { state_ = State::kFailed; }
                             }});
                   }},
               address_);
  }
#else
  void ResolveAddress() {
    std::visit(
        reflect::OverrideFunc{[this](IpAddressPortProtocol const& ip_address) {
          ip_addresses_.push_back(ip_address);
          state_ = State::kBuildersCreate;
        }},
        address_);
  }
#endif

  void CreateBuilders() {
    IPoller::ptr poller = poller_.Lock();
    assert(poller);
    for (auto const& addr : ip_addresses_) {
      auto builder = std::make_unique<EthernetTransportBuilder>(action_context_,
                                                                poller, addr);
      transport_builders_.push_back(std::move(builder));
    }
    state_ = State::kBuildersCreated;
  }

  ActionContext action_context_;
  UnifiedAddress address_;
  PtrView<DnsResolver> resolver_;
  PtrView<IPoller> poller_;
  StateMachine<State> state_;
  std::vector<IpAddressPortProtocol> ip_addresses_;
  std::vector<std::unique_ptr<ITransportStreamBuilder>> transport_builders_;
  Subscription address_resolve_sub_;
};
}  // namespace ethernet_access_point_internal

ActionPtr<TransportBuilderAction> EthernetChannel::TransportBuilder() {
  DnsResolver::ptr dns_resolver = dns_resolver_;
  IPoller::ptr poller = poller_;
  assert(dns_resolver);
  assert(poller);
  return ActionPtr<
      ethernet_access_point_internal::EthernetTransportBuilderAction>(
      *aether_.as<Aether>(), address, dns_resolver, poller);
}

}  // namespace ae
