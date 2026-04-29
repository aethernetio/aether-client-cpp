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
#if AE_SUPPORT_WIFIS

#  include <limits>
#  include <utility>
#  include <cstdint>

#  include "aether/ptr/ptr_view.h"
#  include "aether/actions/action.h"
#  include "aether/executors/executors.h"
#  include "aether/events/event_subscription.h"

#  include "aether/aether.h"
#  include "aether/poller/poller.h"
#  include "aether/dns/dns_resolve.h"

#  include "aether/channels/ethernet_transport_factory.h"

#  include "aether/tele/tele.h"

namespace ae {
namespace wifi_channel_internal {
ex::sender auto WifiConnect(Ptr<WifiAccessPoint> const& access_point) {
  return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
      [ap{PtrView<WifiAccessPoint>{access_point}},
       connect_sub{Subscription{}}](auto& ctx) mutable noexcept {
        auto access_point = ap.Lock();
        assert(access_point && "Wifi access point is not loaded");
        connect_sub = access_point->Connect().connection_event().Subscribe(
            [&](bool is_connected) mutable noexcept {
              if (is_connected) {
                ex::set_value(std::move(ctx.receiver));
              } else {
                ex::set_error(std::move(ctx.receiver), 1);
              }
            });
      });
}

using ResolveSender =
    ex::AnySender<ex::set_value_t(std::vector<Endpoint>), ex::set_error_t(int)>;

ResolveSender ResolveAddress(PtrView<DnsResolver> const& resolver,
                             NamedAddr const& addr, std::uint16_t port,
                             Protocol protocol) {
#  if AE_SUPPORT_CLOUD_DNS
  auto resolver_ptr = resolver.Lock();
  assert(resolver_ptr && "Dns resolver is not loaded");
  return resolver_ptr->Resolve(addr, port, protocol);
#  else
  // named address doesn't supported
  return ex::just_error(1);
#  endif
}

ResolveSender ResolveAddress(PtrView<DnsResolver> const&, auto const& addr,
                             std::uint16_t port, Protocol protocol) {
  return ex::just(std::vector<Endpoint>{Endpoint{addr, port, protocol}});
}

auto CreateTransport(AeContext const& context, PtrView<IPoller> const& poller,
                     Endpoint const& e) {
  auto poller_ptr = poller.Lock();
  assert(poller_ptr);
  return EthernetTransportFactory::Create(context.aether(), poller_ptr, e);
}

ex::sender auto TransportConnect(std::unique_ptr<ByteIStream>&& stream) {
  return ex::create<ex::set_value_t(std::unique_ptr<ByteIStream>),
                    ex::set_error_t(int)>(
      [s{std::move(stream)},
       link_sub{Subscription{}}](auto& ctx) mutable noexcept {
        // if already linked return stream
        switch (s->stream_info().link_state) {
          case LinkState::kLinked: {
            ex::set_value(std::move(ctx.receiver), std::move(s));
            return;
          }
          case LinkState::kLinkError: {
            ex::set_error(std::move(ctx.receiver), 1);
            return;
          }
          default:
            break;
        }
        // wait till linked
        link_sub = s->stream_update_event().Subscribe([&]() mutable noexcept {
          link_sub.Reset();
          if (s->stream_info().link_state == LinkState::kLinked) {
            ex::set_value(std::move(ctx.receiver), std::move(s));
          } else {
            ex::set_error(std::move(ctx.receiver), 2);
          }
        });
      });
}

ex::sender auto MakeTransportBuilder(AeContext ae_context,
                                     Ptr<DnsResolver> const& dns_resolver,
                                     Ptr<IPoller> const& poller,
                                     Ptr<WifiAccessPoint> const& access_point,
                                     Endpoint address) noexcept {
  return  // connect to wifi first
      WifiConnect(access_point)
      // resolve named address to ip address
      | ex::let_value([a{std::move(address)},
                       r{PtrView<DnsResolver>{dns_resolver}}]() noexcept {
          return std::visit(
              [&](auto const& addr) noexcept {
                return ResolveAddress(r, addr, a.port, a.protocol);
              },
              a.address);
        })
      // create transport and wait till it connected
      // TODO: add selection for endpoint to connect
      | ex::let_value([c{ae_context}, p{PtrView<IPoller>{poller}}](
                          std::vector<Endpoint> const& endpoints) noexcept {
          return ex::for_range(Iter{endpoints.begin(), endpoints.end()},
                               [c, p](auto const& e) noexcept {
                                 return ex::just(e) |
                                        ex::then([&](auto const& e) noexcept {
                                          return CreateTransport(c, p, e);
                                        }) |
                                        ex::let_value([](auto& s) noexcept {
                                          return TransportConnect(std::move(s));
                                        }) |
                                        ex::upon_error([](auto&&...) noexcept {
                                          return ex::for_continue;
                                        });
                               }) |
                 ex::let_stopped([]() noexcept { return ex::just_error(1); });
        });
}
}  // namespace wifi_channel_internal

WifiChannel::WifiChannel(ObjProp prop, ObjPtr<Aether> aether,
                         ObjPtr<IPoller> poller, ObjPtr<DnsResolver> resolver,
                         WifiAccessPoint::ptr access_point, Endpoint address)
    : Channel{prop},
      address{std::move(address)},
      aether_(std::move(aether)),
      poller_(std::move(poller)),
      resolver_(std::move(resolver)),
      access_point_(std::move(access_point)) {
  // fill transport properties
  auto protocol = address.protocol;

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
  auto connected =
      access_point_.WithLoaded([&](auto const& p) { return p->IsConnected(); });
  if (connected.value_or(false)) {
    return channel_statistics_->connection_time_statistics().percentile<99>();
  }
  // add time required for wifi connection
  return channel_statistics_->connection_time_statistics().percentile<99>() +
         std::chrono::milliseconds{AE_WIFI_CONNECTION_TIMEOUT_MS};
}

TransportBuildSender WifiChannel::TransportBuilder() {
  auto resolver = resolver_.Load();
#  if AE_SUPPORT_CLOUD_DNS
  assert(resolver && "Resolver is not loaded");
#  endif

  auto poller = poller_.Load();
  auto access_point = access_point_.Load();

  assert(poller && "Poller is not loaded");
  assert(access_point && "Access point is not loaded");

  return wifi_channel_internal::MakeTransportBuilder(
      *aether_.Load().as<Aether>(), resolver, poller, access_point, address);
}

}  // namespace ae
#endif
