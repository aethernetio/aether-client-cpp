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

#include "aether/poller/poller.h"

// IWYU pragma: begin_keeps
#include "aether/transport/low_level/tcp/tcp.h"
#include "aether/transport/low_level/udp/udp.h"
// IWYU pragma: end_keeps

namespace ae {
std::unique_ptr<ByteIStream> EthernetTransportFactory::Create(
    ActionContext action_context, ObjPtr<IPoller> const& poller,
    IpAddressPortProtocol address_port_protocol) {
  switch (address_port_protocol.protocol) {
    case Protocol::kTcp:
      return BuildTcp(action_context, poller, address_port_protocol);
    case Protocol::kUdp:
      return BuildUdp(action_context, poller, address_port_protocol);
    default:
      assert(false);
      return nullptr;
  }
}

std::unique_ptr<ByteIStream> EthernetTransportFactory::BuildTcp(
    [[maybe_unused]] ActionContext action_context,
    [[maybe_unused]] ObjPtr<IPoller> const& poller,
    [[maybe_unused]] IpAddressPortProtocol address_port_protocol) {
#if defined COMMON_TCP_TRANSPORT_ENABLED
  return std::make_unique<TcpTransport>(action_context, poller,
                                        address_port_protocol);
#else
  static_assert(false, "No transport enabled");
#endif
}

std::unique_ptr<ByteIStream> EthernetTransportFactory::BuildUdp(
    [[maybe_unused]] ActionContext action_context,
    [[maybe_unused]] ObjPtr<IPoller> const& poller,
    [[maybe_unused]] IpAddressPortProtocol address_port_protocol) {
#if defined COMMON_UDP_TRANSPORT_ENABLED
  return std::make_unique<UdpTransport>(action_context, poller,
                                        address_port_protocol);
#else
  static_assert(false, "No transport enabled");
#endif
}

}  // namespace ae
