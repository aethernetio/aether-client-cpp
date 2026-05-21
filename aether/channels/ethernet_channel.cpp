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

#include <limits>
#include <utility>

#include "aether/memory.h"
#include "aether/events/event_subscription.h"

#include "aether/aether.h"
#include "aether/poller/poller.h"
#include "aether/dns/dns_resolve.h"
#include "aether/executors/executors.h"

#include "aether/channels/ethernet_transport_factory.h"

namespace ae {
namespace ethernet_access_point_internal {
using ResolveSender =
    ex::AnySender<ex::set_value_t(std::vector<Endpoint>), ex::set_error_t(int)>;

ResolveSender ResolveAddress(Ptr<DnsResolver> const& resolver,
                             NamedAddr const& addr, std::uint16_t port,
                             Protocol protocol) {
#if AE_SUPPORT_CLOUD_DNS
  return resolver->Resolve(addr, port, protocol);
#else
  // named address doesn't supported
  return ex::just_error(1);
#endif
}

ResolveSender ResolveAddress(Ptr<DnsResolver> const&, auto const& addr,
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
      [&, s{std::move(stream)},
       link_sub{Subscription{}}](auto& ctx) mutable noexcept {
        auto handle_link_state = [&]() noexcept {
          link_sub.Reset();
          switch (s->stream_info().link_state) {
            case LinkState::kLinked: {
              ex::set_value(std::move(ctx.receiver), std::move(s));
              return true;
            }
            case LinkState::kLinkError: {
              ex::set_error(std::move(ctx.receiver), 1);
              return true;
            }
            default:
              return false;
          }
        };

        // if already linked return stream
        if (handle_link_state()) {
          return;
        }
        // wait till linked
        link_sub = s->stream_update_event().Subscribe(handle_link_state);
      });
}

ex::sender auto MakeTransportBuilder(AeContext ae_context,
                                     Ptr<DnsResolver> const& dns_resolver,
                                     Ptr<IPoller> const& poller,
                                     Endpoint address) noexcept {
  // resolve named address to ip address
  return std::visit(
             [&](auto const& addr) noexcept {
               return ResolveAddress(dns_resolver, addr, address.port,
                                     address.protocol);
             },
             address.address) |
         // create transport and wait till it connected
         ex::let_value([c{ae_context}, p{PtrView<IPoller>{poller}}](
                           auto const& endpoints) noexcept {
           return ex::for_range(
                      Iter{endpoints.begin(), endpoints.end()},
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
}  // namespace ethernet_access_point_internal

EthernetChannel::EthernetChannel() = default;

EthernetChannel::EthernetChannel(ObjProp prop, ObjPtr<Aether> aether,
                                 ObjPtr<DnsResolver> dns_resolver,
                                 ObjPtr<IPoller> poller, Endpoint address)
    : Channel{prop},
      address{std::move(address)},
      aether_{std::move(aether)},
      poller_{std::move(poller)},
      dns_resolver_{std::move(dns_resolver)} {
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

EthernetChannel::~EthernetChannel() = default;

TransportBuildSender EthernetChannel::TransportBuilder() {
  auto resolver = dns_resolver_.Load();
#if AE_SUPPORT_CLOUD_DNS
  assert(resolver && "Resolver is not loaded");
#endif
  auto poller = poller_.Load();
  assert(poller && "Poller is not loaded");

  return ethernet_access_point_internal::MakeTransportBuilder(
      AeContext{*aether_.Load().as<Aether>()}, resolver, poller, address);
}

}  // namespace ae
