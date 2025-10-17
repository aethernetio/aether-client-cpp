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

#include "aether/channels/ethernet_transport_factory.h"

namespace ae {
namespace wifi_channel_internal {
class WifiTransportBuilderAction final : public TransportBuilderAction {
 public:
  enum class State : std::uint8_t {
    kWifiConnect,
    kAddressResolve,
    kTransportCreate,
    kWaitTransportConnect,
    kTransportConnected,
    kFailed,
  };

  WifiTransportBuilderAction(ActionContext action_context, WifiChannel& channel,
                             WifiAccessPoint::ptr const& access_point,
                             IPoller::ptr const& poller,
                             DnsResolver::ptr const& resolver,
                             UnifiedAddress address)
      : TransportBuilderAction{action_context},
        action_context_{action_context},
        channel_{&channel},
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
        case State::kTransportCreate:
          CreateTransport();
          break;
        case State::kWaitTransportConnect:
          break;
        case State::kTransportConnected:
          return UpdateStatus::Result();
        case State::kFailed:
          return UpdateStatus::Error();
      }
    }
    return {};
  }

  std::unique_ptr<ByteIStream> transport_stream() override {
    return std::move(transport_stream_);
  }

 private:
  void WifiConnect() {
    auto access_point = access_point_.Lock();
    assert(access_point);
    auto connect_action = access_point->Connect();
    wifi_connected_sub_ = connect_action->StatusEvent().Subscribe(ActionHandler{
        OnResult{[this]() {
          // build transport start after wifi is connected
          start_time_ = Now();
          state_ = State::kAddressResolve;
          Action::Trigger();
        }},
        OnError{[this]() {
          state_ = State::kFailed;
          Action::Trigger();
        }},
    });
  }

  void ResolveAddress() {
    std::visit([this](auto const& addr) { DoResolverAddress(addr); }, address_);
  }

#if AE_SUPPORT_CLOUD_DNS
  void DoResolverAddress(NameAddress const& name_address) {
    auto dns_resolver = resolver_.Lock();
    assert(dns_resolver);
    auto resolve_action = dns_resolver->Resolve(name_address);

    address_resolve_sub_ = resolve_action->StatusEvent().Subscribe(
        ActionHandler{OnResult{[this](auto& action) {
                        ip_addresses_ = std::move(action.addresses);
                        it_ = std::begin(ip_addresses_);
                        state_ = State::kTransportCreate;
                        Action::Trigger();
                      }},
                      OnError{[this]() {
                        state_ = State::kFailed;
                        Action::Trigger();
                      }}});
  }
#endif

  void DoResolverAddress(IpAddressPortProtocol const& ip_address) {
    ip_addresses_.push_back(ip_address);
    it_ = std::begin(ip_addresses_);
    state_ = State::kTransportCreate;
    Action::Trigger();
  }

  void CreateTransport() {
    state_ = State::kWaitTransportConnect;

    if (it_ == std::end(ip_addresses_)) {
      state_ = State::kFailed;
      Action::Trigger();
      return;
    }

    IPoller::ptr poller = poller_.Lock();
    assert(poller);
    auto& addr = *(it_++);
    transport_stream_ =
        EthernetTransportFactory::Create(action_context_, poller, addr);
    if (!transport_stream_) {
      // try next address
      state_ = State::kTransportCreate;
      Action::Trigger();
      return;
    }
    if (transport_stream_->stream_info().link_state == LinkState::kLinked) {
      Connected();
      return;
    }
    transport_sub_ =
        transport_stream_->stream_update_event().Subscribe([this]() {
          if (transport_stream_->stream_info().link_state ==
              LinkState::kLinked) {
            // transport connected
            Connected();
          } else if (transport_stream_->stream_info().link_state ==
                     LinkState::kLinkError) {
            // try next address
            state_ = State::kTransportCreate;
            Action::Trigger();
          }
        });
  }

  void Connected() {
    transport_sub_.Reset();
    auto built_time = std::chrono::duration_cast<Duration>(Now() - start_time_);
    AE_TELED_DEBUG("Transport built by {:%S}", built_time);
    channel_->channel_statistics().AddConnectionTime(built_time);
    state_ = State::kTransportConnected;
    Action::Trigger();
  }

  ActionContext action_context_;
  WifiChannel* channel_;
  PtrView<WifiAccessPoint> access_point_;
  PtrView<IPoller> poller_;
  PtrView<DnsResolver> resolver_;
  UnifiedAddress address_;

  std::vector<IpAddressPortProtocol> ip_addresses_;
  std::vector<IpAddressPortProtocol>::iterator it_;
  std::unique_ptr<ByteIStream> transport_stream_;
  Subscription wifi_connected_sub_;
  Subscription address_resolve_sub_;
  Subscription transport_sub_;
  StateMachine<State> state_;
  TimePoint start_time_;
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

Duration WifiChannel::TransportBuildTimeout() const {
  if (access_point_->IsConnected()) {
    return channel_statistics_->connection_time_statistics().percentile<99>();
  }
  // add time required for wifi connection
  return channel_statistics_->connection_time_statistics().percentile<99>() +
         std::chrono::milliseconds{AE_WIFI_CONNECTION_TIMEOUT_MS};
}

ActionPtr<TransportBuilderAction> WifiChannel::TransportBuilder() {
  IPoller::ptr poller = poller_;
  DnsResolver::ptr resolver = resolver_;

  return ActionPtr<wifi_channel_internal::WifiTransportBuilderAction>{
      *aether_.as<Aether>(), *this, access_point_, poller, resolver, address};
}

}  // namespace ae
