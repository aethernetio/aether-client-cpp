// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/config.h"
#if AE_SUPPORT_P2P
#  ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#      define NOMINMAX
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>

#    include <iphlpapi.h>
#  else
#    include <arpa/inet.h>
#    include <fcntl.h>
#    include <ifaddrs.h>
#    include <net/if.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
#    include <cerrno>
#  endif
#  include <algorithm>
#  include <cstring>
#  include <stdexcept>
#  include "aether/peer/peer_udp.h"
namespace ae {
namespace peer_udp_internal {
#  ifdef _WIN32
using Socket = SOCKET;
using SocketSize = int;
constexpr Socket kInvalid = INVALID_SOCKET;
void Close(Socket socket) { closesocket(socket); }
void Initialize() {
  static auto const initialized = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  if (!initialized) {
    throw std::runtime_error("Peer Winsock startup failed");
  }
}
#  else
using Socket = int;
using SocketSize = std::size_t;
constexpr Socket kInvalid = -1;
void Close(Socket socket) { close(socket); }
void Initialize() {}
#  endif
in_addr NativeAddress(Address const& address) {
  in_addr native{};
  auto const& ipv4 = std::get<IpV4Addr>(address);
  std::memcpy(&native, ipv4.ipv4_value, 4);
  return native;
}
Address FromNative(in_addr address) {
  IpV4Addr ipv4{};
  std::memcpy(ipv4.ipv4_value, &address, 4);
  return Address{ipv4};
}
sockaddr_in ToNative(AddressPort const& endpoint) {
  sockaddr_in result{};
  result.sin_family = AF_INET;
  result.sin_port = htons(endpoint.port);
  result.sin_addr = NativeAddress(endpoint.address);
  return result;
}
}  // namespace peer_udp_internal

struct PeerUdp::Impl {
  peer_udp_internal::Socket socket{peer_udp_internal::kInvalid};
  ~Impl() {
    if (socket != peer_udp_internal::kInvalid) {
      peer_udp_internal::Close(socket);
    }
  }
};
std::vector<Address> PeerUdp::Interfaces() {
  peer_udp_internal::Initialize();
  std::vector<Address> addresses;
#  ifdef _WIN32
  ULONG length = 16384;
  std::vector<std::uint8_t> bytes(length);
  auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(bytes.data());
  auto status = GetAdaptersAddresses(AF_INET, 0, nullptr, adapters, &length);
  if (status == ERROR_BUFFER_OVERFLOW) {
    bytes.resize(length);
    adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(bytes.data());
    status = GetAdaptersAddresses(AF_INET, 0, nullptr, adapters, &length);
  }
  if (status != NO_ERROR) {
    throw std::runtime_error("Cannot enumerate peer interfaces");
  }
  for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp) {
      continue;
    }
    for (auto* item = adapter->FirstUnicastAddress; item; item = item->Next) {
      if (item->Address.lpSockaddr->sa_family == AF_INET) {
        auto* native = reinterpret_cast<sockaddr_in*>(item->Address.lpSockaddr);
        addresses.push_back(peer_udp_internal::FromNative(native->sin_addr));
      }
    }
  }
#  else
  ifaddrs* interfaces{};
  if (getifaddrs(&interfaces) != 0) {
    throw std::runtime_error("Cannot enumerate peer interfaces");
  }
  for (auto* item = interfaces; item; item = item->ifa_next) {
    if (item->ifa_addr && item->ifa_addr->sa_family == AF_INET &&
        (item->ifa_flags & IFF_UP) != 0) {
      auto* native = reinterpret_cast<sockaddr_in*>(item->ifa_addr);
      addresses.push_back(peer_udp_internal::FromNative(native->sin_addr));
    }
  }
  freeifaddrs(interfaces);
#  endif
  std::sort(addresses.begin(), addresses.end());
  addresses.erase(std::unique(addresses.begin(), addresses.end()),
                  addresses.end());
  return addresses;
}
PeerUdp::PeerUdp(Address local, std::uint16_t port, bool discovery)
    : impl_{std::make_unique<Impl>()}, local_{std::move(local)} {
#  if !AE_SUPPORT_LAN
  if (discovery) {
    throw std::invalid_argument("LAN is disabled in this build");
  }
#  endif

  peer_udp_internal::Initialize();
  if (local_.Index() != AddrVersion::kIpV4) {
    throw std::invalid_argument("Peer socket requires IPv4");
  }
  impl_->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (impl_->socket == peer_udp_internal::kInvalid) {
    throw std::runtime_error("Cannot create peer UDP socket");
  }
  auto set = [&](int level, int option, auto const& value) {
    return setsockopt(impl_->socket, level, option,
                      reinterpret_cast<char const*>(&value), sizeof value) == 0;
  };
  // Burst arrivals can queue between scheduler polls. Keep the platform
  // default if the socket cannot raise its receive buffer.
  int const receive_buffer = 512 * 1024;
  int current_receive_buffer{};
#ifdef _WIN32
  int receive_buffer_length = sizeof current_receive_buffer;
#else
  socklen_t receive_buffer_length = sizeof current_receive_buffer;
#endif
  if (getsockopt(impl_->socket, SOL_SOCKET, SO_RCVBUF,
                 reinterpret_cast<char*>(&current_receive_buffer),
                 &receive_buffer_length) != 0 ||
      current_receive_buffer < receive_buffer) {
    static_cast<void>(set(SOL_SOCKET, SO_RCVBUF, receive_buffer));
  }
  int const send_buffer = std::max<int>(AE_SOCKET_SEND_BUFFER_BYTES, 512 * 1024);
  if (!set(SOL_SOCKET, SO_SNDBUF, send_buffer) &&
      !set(SOL_SOCKET, SO_SNDBUF, AE_SOCKET_SEND_BUFFER_BYTES)) {
    throw std::runtime_error("Cannot set peer send buffer");
  }
#  if AE_SUPPORT_LAN
  int one = 1;
  if (discovery && !set(SOL_SOCKET, SO_REUSEADDR, one)) {
    throw std::runtime_error("Cannot share discovery socket");
  }
#  endif
  auto address = peer_udp_internal::ToNative({local_, port});
  if (bind(impl_->socket, reinterpret_cast<sockaddr*>(&address),
           sizeof address) != 0) {
    throw std::runtime_error("Cannot bind peer UDP socket");
  }
#  ifdef _WIN32
  u_long nonblocking = 1;
  if (ioctlsocket(impl_->socket, FIONBIO, &nonblocking) != 0) {
    throw std::runtime_error("Cannot make peer socket nonblocking");
  }
#  else
  int flags = fcntl(impl_->socket, F_GETFL, 0);
  if (flags < 0 || fcntl(impl_->socket, F_SETFL, flags | O_NONBLOCK) != 0) {
    throw std::runtime_error("Cannot make peer socket nonblocking");
  }
#  endif
#  if AE_SUPPORT_LAN
  unsigned char ttl = 1;
  unsigned char loop = 1;
  set(IPPROTO_IP, IP_MULTICAST_TTL, ttl);
  set(IPPROTO_IP, IP_MULTICAST_LOOP, loop);
  if (discovery) {
    bool joined = false;
    for (auto const& iface : Interfaces()) {
      ip_mreq group{};
      inet_pton(AF_INET, "239.255.46.1", &group.imr_multiaddr);
      group.imr_interface = peer_udp_internal::NativeAddress(iface);
      joined |= set(IPPROTO_IP, IP_ADD_MEMBERSHIP, group);
    }
    if (!joined) {
      throw std::runtime_error("Cannot join LAN discovery group");
    }
  } else {
    auto iface = peer_udp_internal::NativeAddress(local_);
    set(IPPROTO_IP, IP_MULTICAST_IF, iface);
  }
#  endif
}
PeerUdp::~PeerUdp() = default;
PeerSendResult PeerUdp::ClassifySendFailure(int error) {
#  ifdef _WIN32
  if (error == WSAEWOULDBLOCK || error == WSAENOBUFS || error == WSAEINTR) {
    return PeerSendResult::kBlocked;
  }
#  else
  if (error == EAGAIN || error == EWOULDBLOCK || error == ENOBUFS ||
      error == EINTR) {
    return PeerSendResult::kBlocked;
  }
#  endif
  return PeerSendResult::kError;
}
bool PeerUdp::Send(AddressPort const& remote, DataBuffer const& data) {
  return TrySend(remote, data) == PeerSendResult::kSent;
}
PeerSendResult PeerUdp::TrySend(AddressPort const& remote,
                                DataBuffer const& data) {
  if (remote.address.Index() != AddrVersion::kIpV4 || data.size() > 65507 ||
      remote.port == 0) {
    return PeerSendResult::kError;
  }
  auto address = peer_udp_internal::ToNative(remote);
  auto sent = sendto(impl_->socket, reinterpret_cast<char const*>(data.data()),
                     static_cast<peer_udp_internal::SocketSize>(data.size()), 0,
                     reinterpret_cast<sockaddr*>(&address), sizeof address);
  if (sent < 0) {
#  ifdef _WIN32
    return ClassifySendFailure(WSAGetLastError());
#  else
    return ClassifySendFailure(errno);
#  endif
  }
  return static_cast<std::size_t>(sent) == data.size() ? PeerSendResult::kSent
                                                       : PeerSendResult::kError;
}
#  if AE_SUPPORT_LAN
bool PeerUdp::Announce(std::uint16_t port, DataBuffer const& data) {
  return Send({Address{IpV4Addr{{239, 255, 46, 1}}}, port}, data);
}
#  endif
std::optional<PeerDatagram> PeerUdp::Receive() {
  std::array<char, 65536> bytes{};
  sockaddr_in source{};
#  ifdef _WIN32
  int length = sizeof source;
#  else
  socklen_t length = sizeof source;
#  endif
  auto received =
      recvfrom(impl_->socket, bytes.data(),
               static_cast<peer_udp_internal::SocketSize>(bytes.size()), 0,
               reinterpret_cast<sockaddr*>(&source), &length);
  if (received <= 0) {
    return {};
  }
  PeerDatagram packet;
  packet.source = {peer_udp_internal::FromNative(source.sin_addr),
                   ntohs(source.sin_port)};
  auto* first = reinterpret_cast<std::uint8_t*>(bytes.data());
  packet.data.assign(first, first + received);
  return packet;
}
std::uint16_t PeerUdp::port() const {
  sockaddr_in address{};
#  ifdef _WIN32
  int length = sizeof address;
#  else
  socklen_t length = sizeof address;
#  endif
  if (getsockname(impl_->socket, reinterpret_cast<sockaddr*>(&address),
                  &length) != 0) {
    throw std::runtime_error("Cannot query peer UDP port");
  }
  return ntohs(address.sin_port);
}
}  // namespace ae
#endif
