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

#include "aether/channels/ethernet_transport_builder.h"

#include "aether/poller/poller.h"

// IWYU pragma: begin_keeps
#include "aether/transport/low_level/tcp/tcp.h"
#include "aether/transport/low_level/udp/udp.h"
// IWYU pragma: end_keeps

namespace ae {
EthernetTransportBuilder::EthernetTransportBuilder(
    ActionContext action_context, ObjPtr<IPoller> const& poller,
    IpAddressPortProtocol address_port_protocol)
    : action_context_{action_context},
      poller_{poller},
      address_port_protocol_{std::move(address_port_protocol)} {}

std::unique_ptr<ByteIStream> EthernetTransportBuilder::BuildTransportStream() {
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

std::unique_ptr<ByteIStream> EthernetTransportBuilder::BuildTcp() {
#if defined COMMON_TCP_TRANSPORT_ENABLED
  IPoller::ptr poller = poller_.Lock();
  return std::make_unique<TcpTransport>(action_context_, poller,
                                        address_port_protocol_);
#else
  static_assert(false, "No transport enabled");
#endif
}

std::unique_ptr<ByteIStream> EthernetTransportBuilder::BuildUdp() {
#if defined COMMON_UDP_TRANSPORT_ENABLED
  IPoller::ptr poller = poller_.Lock();
  return std::make_unique<UdpTransport>(action_context_, poller,
                                        address_port_protocol_);
#else
  static_assert(false, "No transport enabled");
#endif
}

}  // namespace ae
