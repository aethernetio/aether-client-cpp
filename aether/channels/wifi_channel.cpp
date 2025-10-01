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

#include "aether/channels/wifi_channel.h"

#include <limits>
#include <utility>
#include <cstdint>

#include "aether/ptr/ptr_view.h"
#include "aether/actions/action.h"
#include "aether/types/state_machine.h"
#include "aether/events/event_subscription.h"

#include "aether/aether.h"
#include "aether/poller/poller.h"
#include "aether/dns/dns_resolve.h"

#include "aether/channels/ethernet_transport_builder.h"

namespace ae {
namespace wifi_channel_internal {
class WifiTransportBuilderAction final : public TransportBuilderAction {
 public:
  enum class State : std::uint8_t {
    kAddressResolve,
    kWifiConnect,
    kBuildersCreate,
    kBuildersReady,
    kError,
  };

  WifiTransportBuilderAction(ActionContext action_context,
                             WifiAccessPoint::ptr const& access_point,
                             IPoller::ptr const& poller,
                             DnsResolver::ptr const& resolver,
                             UnifiedAddress address)
      : TransportBuilderAction{action_context},
        action_context_{action_context},
        access_point_{access_point},
        poller_{poller},
        resolver_{resolver},
        address_{std::move(address)},
        state_{State::kWifiConnect} {}

  UpdateStatus Update() override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kWifiConnect:
          WifiConnect();
          break;
        case State::kAddressResolve:
          ResolveAddress();
          break;
        case State::kBuildersCreate:
          BuildersCreate();
          break;
        case State::kBuildersReady:
          return UpdateStatus::Result();
        case State::kError:
          return UpdateStatus::Error();
      }
    }
    return {};
  }

  std::vector<std::unique_ptr<ITransportStreamBuilder>> builders() override {
    return std::move(transport_builders_);
  }

 private:
  void WifiConnect() {
    auto access_point = access_point_.Lock();
    assert(access_point);
    auto connect_action = access_point->Connect();
    wifi_connected_sub_ = connect_action->StatusEvent().Subscribe(ActionHandler{
        OnResult{[this]() {
          state_ = State::kAddressResolve;
          Action::Trigger();
        }},
        OnError{[this]() {
          state_ = State::kError;
          Action::Trigger();
        }},
    });
  }

#if AE_SUPPORT_CLOUD_DNS
  void ResolveAddress() {
    std::visit(reflect::OverrideFunc{
                   [this](IpAddressPortProtocol const& ip_address) {
                     ip_addresses_.push_back(ip_address);
                     state_ = State::kBuildersCreate;
                     Action::Trigger();
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
                               Action::Trigger();
                             }},
                             OnError {
                               [this]() {
                                 state_ = State::kError;
                                 Action::Trigger();
                               }
                             }});
                   }},
               address_);
  }
#else
  void ResolveAddress() {
    std::visit(
        reflect::OverrideFunc{[this](IpAddressPortProtocol const& ip_address) {
          ip_addresses_.push_back(ip_address);
        }},
        address_);
    state_ = State::kBuildersCreate;
    Action::Trigger();
  }
#endif

  void BuildersCreate() {
    transport_builders_.reserve(ip_addresses_.size());
    IPoller::ptr poller = poller_.Lock();
    assert(poller);
    for (auto const& addr : ip_addresses_) {
      transport_builders_.emplace_back(
          std::make_unique<EthernetTransportBuilder>(action_context_, poller,
                                                     addr));
    }
    state_ = State::kBuildersReady;
    Action::Trigger();
  }

  ActionContext action_context_;
  PtrView<WifiAccessPoint> access_point_;
  PtrView<IPoller> poller_;
  PtrView<DnsResolver> resolver_;
  UnifiedAddress address_;

  std::vector<IpAddressPortProtocol> ip_addresses_;
  std::vector<std::unique_ptr<ITransportStreamBuilder>> transport_builders_;
  Subscription wifi_connected_sub_;
  Subscription address_resolve_sub_;
  StateMachine<State> state_;
};
}  // namespace wifi_channel_internal

WifiChannel::WifiChannel(ObjPtr<Aether> aether, ObjPtr<IPoller> poller,
                         ObjPtr<DnsResolver> resolver,
                         WifiAccessPoint::ptr access_point,
                         UnifiedAddress address, Domain* domain)
    : Channel{std::move(address), domain},
      aether_(std::move(aether)),
      poller_(std::move(poller)),
      resolver_(std::move(resolver)),
      access_point_(std::move(access_point)) {
  // fill transport properties
  auto protocol = std::visit([](auto&& adr) { return adr.protocol; }, address);

  switch (protocol) {
    case Protocol::kTcp: {
      transport_properties_.connection_type = ConnectionType::kConnectionFull;
      transport_properties_.max_packet_size =
          std::numeric_limits<std::uint32_t>::max();
      transport_properties_.rec_packet_size = 1500;
      transport_properties_.reliability = Reliability::kReliable;
      break;
    }
    case Protocol::kUdp: {
      transport_properties_.connection_type = ConnectionType::kConnectionLess;
      transport_properties_.max_packet_size = 1200;
      transport_properties_.rec_packet_size = 1200;
      transport_properties_.reliability = Reliability::kUnreliable;
      break;
    }
    default:
      // protocol is not supported
      assert(false);
  }
}

ActionPtr<TransportBuilderAction> WifiChannel::TransportBuilder() {
  IPoller::ptr poller = poller_;
  DnsResolver::ptr resolver = resolver_;

  return ActionPtr<wifi_channel_internal::WifiTransportBuilderAction>{
      *aether_.as<Aether>(), access_point_, poller, resolver, address};
}

}  // namespace ae
