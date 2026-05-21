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

#include "aether/channels/modem_channel.h"
#if AE_SUPPORT_MODEMS

#  include <utility>

#  include "aether/config.h"
#  include "aether/memory.h"
#  include "aether/aether.h"
#  include "aether/executors/executors.h"
#  include "aether/transport/modems/modem_transport.h"

namespace ae {
namespace modem_channel_internal {
auto EnsureModemConnected(ModemAccessPoint& access_point) {
  return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
      [&, s{Subscription{}}](auto& ctx) mutable noexcept {
        auto& connect_action = access_point.Connect();
        s = connect_action.connection_event().Subscribe(
            [&](bool is_connected) noexcept {
              if (is_connected) {
                ex::set_value(std::move(ctx.receiver));
              } else {
                ex::set_error(std::move(ctx.receiver), 1);
              }
            });
      });
}

std::unique_ptr<ByteIStream> CreateTransport(AeContext const& ae_context,
                                             ModemAccessPoint& access_point,
                                             Endpoint&& endpoint) {
  return std::make_unique<ModemTransport>(
      ae_context, access_point.modem_driver(), std::move(endpoint));
}

auto ConnectTransport(std::unique_ptr<ByteIStream>&& transport) {
  return ex::create<ex::set_value_t(std::unique_ptr<ByteIStream>),
                    ex::set_error_t(int)>(
      [t{std::move(transport)}, s{Subscription{}}](auto& ctx) mutable noexcept {
        if (t->stream_info().link_state == LinkState::kLinked) {
          ex::set_value(std::move(ctx.receiver), std::move(t));
          return;
        }

        s = t->stream_update_event().Subscribe([&]() {
          if (t->stream_info().link_state == LinkState::kLinked) {
            ex::set_value(std::move(ctx.receiver), std::move(t));
          } else if (t->stream_info().link_state == LinkState::kLinkError) {
            ex::set_error(std::move(ctx.receiver), 2);
          }
        });
      });
}

auto MakeTransportBuilderSender(AeContext const& ae_context,
                                ModemAccessPoint& access_point,
                                Endpoint endpoint) {
  return EnsureModemConnected(access_point) |
         ex::then([ae_context, &access_point,
                   e{std::move(endpoint)}]() mutable noexcept {
           return CreateTransport(ae_context, access_point, std::move(e));
         }) |
         ex::let_value([&](std::unique_ptr<ByteIStream>& t) noexcept {
           return ConnectTransport(std::move(t));
         });
}

}  // namespace modem_channel_internal

ModemChannel::ModemChannel(ObjProp prop, ObjPtr<Aether> aether,
                           ModemAccessPoint::ptr access_point, Endpoint address)
    : Channel{prop},
      address{std::move(address)},
      aether_{std::move(aether)},
      access_point_{std::move(access_point)} {
  // fill transport properties
  transport_properties_.max_packet_size = 1024;
  transport_properties_.rec_packet_size = 1024;
  switch (address.protocol) {
    case Protocol::kTcp: {
      transport_properties_.connection_type = ConnectionType::kConnectionFull;
      transport_properties_.reliability = Reliability::kReliable;
      break;
    }
    case Protocol::kUdp: {
      transport_properties_.connection_type = ConnectionType::kConnectionLess;
      transport_properties_.reliability = Reliability::kUnreliable;
      break;
    }
    default:
      // protocol is not supported
      assert(false);
  }
}

TransportBuildSender ModemChannel::TransportBuilder() {
  auto ap = access_point_.Load();
  assert(ap && "Access point is not loaded");
  return modem_channel_internal::MakeTransportBuilderSender(
      *aether_.Load().as<Aether>(), *ap, address);
}

Duration ModemChannel::TransportBuildTimeout() const {
  return channel_statistics_->connection_time_statistics().percentile<99>() +
         std::chrono::milliseconds{AE_MODEM_CONNECTION_TIMEOUT_MS};
}

}  // namespace ae
#endif
