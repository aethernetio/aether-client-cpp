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

class EthernetTransportBuilder final : public ITransportBuilder {
 public:
  EthernetTransportBuilder(ActionContext action_context,
                           EthernetChannel& channel, Endpoint address,
                           DnsResolver::ptr const& dns_resolver,
                           IPoller::ptr const& poller)
      : action_context_{action_context},
        ethernet_channel_{&channel},
        address_{std::move(address)},
        resolver_{dns_resolver},
        poller_{poller},
        start_time_{Now()} {}

 private:
  auto ResolveEndpoints(Endpoint const& endpoint) {
    return std::visit(
        reflect::OverrideFunc{
            [&](NamedAddr const& name_address)
                -> ex::any_sender_of<std::vector<Endpoint>> {
#if AE_SUPPORT_CLOUD_DNS
              auto dns_resolver = resolver_.Lock();
              assert(dns_resolver);
              return dns_resolver->Resolve(name_address, endpoint.port,
                                           endpoint.protocol);
#else
              AE_TELED_ERROR("Unable to resolve named address");
              return ex::just_error(ex::make_error());
#endif
            },
            [&](auto const&) -> ex::any_sender_of<std::vector<Endpoint>> {
              return ex::just(std::vector<Endpoint>{endpoint});
            }},
        endpoint.address);
  }

  auto CreateTransport(Endpoint const& endpoint) {
    return ex::create<std::unique_ptr<ByteIStream>>([this,
                                                     endpoint](auto receiver) {
      IPoller::ptr poller = poller_.Lock();
      assert(poller);
      auto transport =
          EthernetTransportFactory::Create(action_context_, poller, endpoint);

      auto current_state = transport->stream_info().link_state;
      if (current_state == LinkState::kLinked) {
        ex::set_value(std::move(receiver), std::move(transport));
        return;
      }
      if (current_state == LinkState::kLinkError) {
        ex::set_error(std::move(receiver), ex::make_error());
        return;
      }

      transport_stream_sub_ = transport->stream_update_event().Subscribe(
          [receiver{std::make_unique<decltype(receiver)>(std::move(receiver))},
           transport{std::move(transport)}]() mutable {
            if (transport->stream_info().link_state == LinkState::kLinked) {
              ex::set_value(std::move(*receiver), std::move(transport));
            } else {
              ex::set_error(std::move(*receiver), ex::make_error());
            }
          });
    });
  }

  auto MakeFirstTransport(std::vector<Endpoint> endpoints) {
    return CreateTransport(endpoints[0]);
    // auto max = endpoints.size();
    // return ex::retry_when(
    //     ex::just_from([this, endpoints{std::move(endpoints)},
    //                    i{std::size_t{}}]() mutable {
    //       return CreateTransport(endpoints[i++]);
    //     }),
    //     [i{std::size_t{}},
    //      max](auto) mutable ->
    //      ex::any_sender_of<std::unique_ptr<ByteIStream>> {
    //       if (i++ > max) {
    //         return ex::just_error(ex::make_error());
    //       }
    //       return ex::just();
    //     });
  }

 public:
  ex::any_sender_of<std::unique_ptr<ByteIStream>> CreateTransport() override {
    return ResolveEndpoints(address_) |
           ex::let_value([this](std::vector<Endpoint> endpoints) {
             return MakeFirstTransport(std::move(endpoints));
           }) |
           ex::then([this](std::unique_ptr<ByteIStream> transport) {
             auto built_time =
                 std::chrono::duration_cast<Duration>(Now() - start_time_);
             AE_TELED_DEBUG("Transport built by {:%S}", built_time);
             return transport;
           });
  }

  ActionContext action_context_;
  EthernetChannel* ethernet_channel_;
  Endpoint address_;
  PtrView<DnsResolver> resolver_;
  PtrView<IPoller> poller_;
  Subscription transport_stream_sub_;

  TimePoint start_time_;
};
}  // namespace ethernet_access_point_internal

EthernetChannel::EthernetChannel(ObjPtr<Aether> aether,
                                 ObjPtr<DnsResolver> dns_resolver,
                                 ObjPtr<IPoller> poller, Endpoint address,
                                 Domain* domain)
    : Channel{domain},
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

std::unique_ptr<ITransportBuilder> EthernetChannel::TransportBuilder() {
  if (!dns_resolver_) {
    aether_->domain_->LoadRoot(dns_resolver_);
  }
  if (!poller_) {
    aether_->domain_->LoadRoot(poller_);
  }

  DnsResolver::ptr dns_resolver = dns_resolver_;
  IPoller::ptr poller = poller_;
  return std::make_unique<
      ethernet_access_point_internal::EthernetTransportBuilder>(
      *aether_.as<Aether>(), *this, address, dns_resolver, poller);
}

}  // namespace ae
