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

#include "aether/config.h"
#include "aether/poller/poller.h"

// IWYU pragma: begin_keeps
#include "aether/transport/system_sockets/tcp/tcp.h"
#include "aether/transport/system_sockets/udp/udp.h"

#include "aether/transport/system_sockets/sockets/lwip_cb_tcp_socket.h"
#include "aether/transport/system_sockets/sockets/lwip_cb_udp_socket.h"
#include "aether/transport/system_sockets/sockets/lwip_tcp_socket.h"
#include "aether/transport/system_sockets/sockets/lwip_udp_socket.h"
#include "aether/transport/system_sockets/sockets/unix_tcp_socket.h"
#include "aether/transport/system_sockets/sockets/unix_udp_socket.h"
#include "aether/transport/system_sockets/sockets/win_tcp_socket.h"
#include "aether/transport/system_sockets/sockets/win_udp_socket.h"
// IWYU pragma: end_keeps

namespace ae {
std::unique_ptr<ByteIStream> EthernetTransportFactory::Create(
    AeContext const& ae_context, Ptr<IPoller> const& poller,
    Endpoint address_port_protocol) {
  assert((address_port_protocol.address.Index() == AddrVersion::kIpV4 ||
          address_port_protocol.address.Index() == AddrVersion::kIpV6) &&
         "Invalid address type");

  switch (address_port_protocol.protocol) {
    case Protocol::kTcp:
      return BuildTcp(ae_context, poller, std::move(address_port_protocol));
    case Protocol::kUdp:
      return BuildUdp(ae_context, poller, std::move(address_port_protocol));
    default:
      assert(false);
      return nullptr;
  }
}

#if AE_SUPPORT_TCP
std::unique_ptr<ByteIStream> EthernetTransportFactory::BuildTcp(
    [[maybe_unused]] AeContext const& ae_context,
    [[maybe_unused]] Ptr<IPoller> const& poller,
    [[maybe_unused]] Endpoint address_port_protocol) {
#  ifdef SYSTEM_SOCKET_TCP_TRANSPORT_ENABLED
#    if LWIP_CB_TCP_SOCKET_ENABLED
  using SocketType = LwipCBTcpSocket;
#    elif LWIP_TCP_SOCKET_ENABLED
  using SocketType = LwipTcpSocket;
#    elif UNIX_SOCKET_ENABLED
  using SocketType = UnixTcpSocket;
#    elif WIN_SOCKET_ENABLED
  using SocketType = WinTcpSocket;
#    endif

  return std::make_unique<TcpTransport<SocketType, AE_TCP_PACKET_QUEUE_SIZE>>(
      ae_context, poller, std::move(address_port_protocol));
#  else
  static_assert(false, "No transport enabled");
#  endif
}
#else
std::unique_ptr<ByteIStream> EthernetTransportFactory::BuildTcp(
    [[maybe_unused]] AeContext const& ae_context,
    [[maybe_unused]] Ptr<IPoller> const& poller,
    [[maybe_unused]] Endpoint address_port_protocol) {
  return nullptr;
}
#endif

#if AE_SUPPORT_UDP
std::unique_ptr<ByteIStream> EthernetTransportFactory::BuildUdp(
    [[maybe_unused]] AeContext const& ae_context,
    [[maybe_unused]] Ptr<IPoller> const& poller,
    [[maybe_unused]] Endpoint address_port_protocol) {
#  ifdef SYSTEM_SOCKET_UDP_TRANSPORT_ENABLED
#    if LWIP_CB_TCP_SOCKET_ENABLED
  using SocketType = LwipCBUdpSocket;
#    elif LWIP_TCP_SOCKET_ENABLED
  using SocketType = LwipUdpSocket;
#    elif UNIX_SOCKET_ENABLED
  using SocketType = UnixUdpSocket;
#    elif WIN_SOCKET_ENABLED
  using SocketType = WinUdpSocket;
#    endif
  return std::make_unique<UdpTransport<SocketType, AE_UDP_PACKET_QUEUE_SIZE>>(
      ae_context, poller, std::move(address_port_protocol));
#  else
  static_assert(false, "No transport enabled");
#  endif
}
#else
std::unique_ptr<ByteIStream> EthernetTransportFactory::BuildUdp(
    [[maybe_unused]] AeContext const& ae_context,
    [[maybe_unused]] Ptr<IPoller> const& poller,
    [[maybe_unused]] Endpoint address_port_protocol) {
  return nullptr;
}
#endif

}  // namespace ae
