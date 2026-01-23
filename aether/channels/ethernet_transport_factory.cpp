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

#include "aether/channels/ethernet_transport_factory.h"

#include <cassert>

#include "aether/poller/poller.h"

// IWYU pragma: begin_keeps
#include "aether/transport/system_sockets/tcp/tcp.h"
#include "aether/transport/system_sockets/udp/udp.h"
// IWYU pragma: end_keeps

namespace ae {
std::unique_ptr<ByteIStream> EthernetTransportFactory::Create(
    ActionContext action_context, Ptr<IPoller> const& poller,
    Endpoint address_port_protocol) {
  assert((address_port_protocol.address.Index() == AddrVersion::kIpV4 ||
          address_port_protocol.address.Index() == AddrVersion::kIpV6) &&
         "Invalid address type");

  switch (address_port_protocol.protocol) {
    case Protocol::kTcp:
      return BuildTcp(action_context, poller, std::move(address_port_protocol));
    case Protocol::kUdp:
      return BuildUdp(action_context, poller, std::move(address_port_protocol));
    default:
      assert(false);
      return nullptr;
  }
}

#if AE_SUPPORT_TCP
std::unique_ptr<ByteIStream> EthernetTransportFactory::BuildTcp(
    [[maybe_unused]] ActionContext action_context,
    [[maybe_unused]] Ptr<IPoller> const& poller,
    [[maybe_unused]] Endpoint address_port_protocol) {
#  if defined COMMON_TCP_TRANSPORT_ENABLED
  return std::make_unique<TcpTransport>(action_context, poller,
                                        std::move(address_port_protocol));
#  else
  static_assert(false, "No transport enabled");
#  endif
}
#else
std::unique_ptr<ByteIStream> EthernetTransportFactory::BuildTcp(
    [[maybe_unused]] ActionContext action_context,
    [[maybe_unused]] Ptr<IPoller> const& poller,
    [[maybe_unused]] Endpoint address_port_protocol) {
  return nullptr;
}
#endif

#if AE_SUPPORT_UDP
std::unique_ptr<ByteIStream> EthernetTransportFactory::BuildUdp(
    [[maybe_unused]] ActionContext action_context,
    [[maybe_unused]] Ptr<IPoller> const& poller,
    [[maybe_unused]] Endpoint address_port_protocol) {
#  if defined SYSTEM_SOCKET_UDP_TRANSPORT_ENABLED
  return std::make_unique<UdpTransport>(action_context, poller,
                                        std::move(address_port_protocol));
#  else
  static_assert(false, "No transport enabled");
#  endif
}
#else
std::unique_ptr<ByteIStream> EthernetTransportFactory::BuildUdp(
    [[maybe_unused]] ActionContext action_context,
    [[maybe_unused]] Ptr<IPoller> const& poller,
    [[maybe_unused]] Endpoint address_port_protocol) {
  return nullptr;
}
#endif

}  // namespace ae
