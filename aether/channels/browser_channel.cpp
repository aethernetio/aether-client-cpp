/*
 * Copyright 2026 Aethernet Inc.
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

#include "aether/channels/browser_channel.h"

#include <cassert>
#include <limits>
#include <utility>

#include "aether/aether.h"
#include "aether/events/event_subscription.h"
#include "aether/executors/executors.h"
#include "aether/memory.h"

#include "aether/channels/browser_transport_factory.h"
#include "aether/transport/browser/browser_endpoint.h"

#include "aether/tele.h"

namespace ae {
namespace browser_channel_internal {

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

        if (handle_link_state()) {
          return;
        }
        link_sub = s->stream_update_event().Subscribe(handle_link_state);
      });
}

ex::sender auto MakeTransportBuilder(AeContext ae_context,
                                     Endpoint address) noexcept {
  return ex::just(std::move(address)) |
         ex::let_value([c{ae_context}](auto const& e) noexcept {
           return ex::just(BrowserTransportFactory::Create(c, e)) |
                  ex::let_value([](auto& s) noexcept {
                    assert(s && "Browser transport create failed");
                    return TransportConnect(std::move(s));
                  });
         });
}
}  // namespace browser_channel_internal

BrowserChannel::BrowserChannel() = default;

BrowserChannel::BrowserChannel(ObjProp prop, ObjPtr<Aether> aether,
                               Endpoint address)
    : Channel{prop}, address{std::move(address)}, aether_{std::move(aether)} {
  assert(IsWebSocketProtocol(address.protocol) ||
         IsHttpProtocol(address.protocol));

  transport_properties_.connection_type = ConnectionType::kConnectionFull;
  transport_properties_.max_packet_size =
      std::numeric_limits<std::uint32_t>::max();
  transport_properties_.rec_packet_size = 1500;
  transport_properties_.reliability = Reliability::kReliable;
}

BrowserChannel::~BrowserChannel() = default;

TransportBuildSender BrowserChannel::TransportBuilder() {
  AE_TELED_DEBUG("Make browser transport builder for {}", address);
  return browser_channel_internal::MakeTransportBuilder(AeContext{*aether_},
                                                        address);
}

}  // namespace ae
