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

#include "aether/transport/system_sockets/sockets/win_udp_socket.h"
#if AE_SUPPORT_UDP && defined WIN_SOCKET_ENABLED

#  include <winsock2.h>
#  include <ws2def.h>
#  include <ws2ipdef.h>
#  include <ws2tcpip.h>

#  include "aether-miscpp/misc/defer.h"
#  include "aether/transport/system_sockets/sockets/get_sock_addr.h"

#  include "aether/tele.h"

#  ifdef AE_BENCH_WIN_UDP_SNDBUF_BYTES
#    include <cstdlib>
#    include <fstream>

namespace {

void WriteBenchUdpSndbufDiag(int requested_bytes, int actual_bytes, int error,
                             bool sndbuf_ok) {
  char const* path = "win_udp_sndbuf_diag.txt";
  char* env_path = nullptr;
  size_t env_len = 0;
  if (_dupenv_s(&env_path, &env_len, "AE_BENCH_UDP_SNDBUF_DIAG") == 0 &&
      env_path != nullptr && env_path[0] != '\0') {
    path = env_path;
  }
  std::ofstream out(path, std::ios::trunc);
  out << "requested_bytes=" << requested_bytes << "\n";
  out << "actual_bytes=" << actual_bytes << "\n";
  out << "setsockopt_wsa_error=" << error << "\n";
  out << "sndbuf_ok=" << (sndbuf_ok ? "true" : "false") << "\n";
  free(env_path);
}

}  // namespace
#  endif

#  if defined(AE_BENCH_UDP_MAX_PACKET_BYTES) && \
      (AE_BENCH_UDP_MAX_PACKET_BYTES > 0)
namespace {

void WriteBenchUdpIpFragDiag(int requested_pmtu, int read_back_pmtu, int error,
                             bool ip_frag_ok) {
  char const* path = "win_udp_ip_frag_diag.txt";
  char* env_path = nullptr;
  size_t env_len = 0;
  if (_dupenv_s(&env_path, &env_len, "AE_BENCH_UDP_IP_FRAG_DIAG") == 0 &&
      env_path != nullptr && env_path[0] != '\0') {
    path = env_path;
  }
  std::ofstream out(path, std::ios::trunc);
  out << "requested_ip_mtu_discover=" << requested_pmtu << "\n";
  out << "read_back_ip_mtu_discover=" << read_back_pmtu << "\n";
  out << "setsockopt_wsa_error=" << error << "\n";
  out << "ip_frag_ok=" << (ip_frag_ok ? "true" : "false") << "\n";
  free(env_path);
}

}  // namespace
#  endif

namespace ae {

// Outbound UDP payloads should stay near MTU to avoid IP fragmentation on send.
// This does not cap WinSocket receive capacity below.
static constexpr std::size_t kWinUdpPreferredSendDatagramBytes = 1200;

// IOCP recv buffer must hold one complete datagram; cloud servers may aggregate
// payloads larger than the preferred send size without changing StreamInfo MTU.
static constexpr std::size_t kWinUdpRecvDatagramBufferBytes = 65536;

WinUdpSocket::WinUdpSocket(Ptr<IPoller> const& poller)
    : WinSocket{*poller, kWinUdpRecvDatagramBufferBytes} {
  bool created = false;

  // ::socket() sets WSA_FLAG_OVERLAPPED by default that allows us to use iocp
  auto sock = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (sock == INVALID_SOCKET) {
    AE_TELED_ERROR("Got socket creation error {}", WSAGetLastError());
    return;
  }
  // close socket on error
  ae_defer[&] {
    if (!created) {
      ::closesocket(sock);
    }
  };

  // make socket non-blocking
  u_long nonblocking = 1;
  if (auto res = ::ioctlsocket(sock, static_cast<long>(FIONBIO), &nonblocking);
      res == SOCKET_ERROR) {
    AE_TELED_ERROR("Socket make non-blocking error {}", WSAGetLastError());
    return;
  }

#  ifdef AE_BENCH_WIN_UDP_SNDBUF_BYTES
  {
    int const requested = AE_BENCH_WIN_UDP_SNDBUF_BYTES;
    int actual = 0;
    int wsa_error = 0;
    bool ok = false;
    if (::setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
                     reinterpret_cast<char const*>(&requested),
                     sizeof(requested)) == SOCKET_ERROR) {
      wsa_error = static_cast<int>(WSAGetLastError());
      WriteBenchUdpSndbufDiag(requested, -1, wsa_error, false);
    } else {
      int len = sizeof(actual);
      if (::getsockopt(sock, SOL_SOCKET, SO_SNDBUF,
                       reinterpret_cast<char*>(&actual), &len) ==
          SOCKET_ERROR) {
        wsa_error = static_cast<int>(WSAGetLastError());
        WriteBenchUdpSndbufDiag(requested, -1, wsa_error, false);
      } else {
        ok = actual >= (requested / 2);
        WriteBenchUdpSndbufDiag(requested, actual, 0, ok);
      }
    }
  }
#  endif

#  if defined(AE_BENCH_UDP_MAX_PACKET_BYTES) && \
      (AE_BENCH_UDP_MAX_PACKET_BYTES > 0)
  {
    DWORD const requested = IP_PMTUDISC_DONT;
    DWORD read_back = 0;
    int wsa_error = 0;
    bool ok = false;
    if (::setsockopt(sock, IPPROTO_IP, IP_MTU_DISCOVER,
                     reinterpret_cast<char const*>(&requested),
                     sizeof(requested)) == SOCKET_ERROR) {
      wsa_error = static_cast<int>(WSAGetLastError());
      WriteBenchUdpIpFragDiag(static_cast<int>(requested), -1, wsa_error, false);
    } else {
      int len = sizeof(read_back);
      if (::getsockopt(sock, IPPROTO_IP, IP_MTU_DISCOVER,
                       reinterpret_cast<char*>(&read_back), &len) ==
          SOCKET_ERROR) {
        wsa_error = static_cast<int>(WSAGetLastError());
        WriteBenchUdpIpFragDiag(static_cast<int>(requested), -1, wsa_error,
                                false);
      } else {
        ok = read_back == IP_PMTUDISC_DONT;
        WriteBenchUdpIpFragDiag(static_cast<int>(requested),
                                static_cast<int>(read_back), 0, ok);
      }
    }
    if (!ok) {
      AE_TELED_ERROR("IP_MTU_DISCOVER IP_PMTUDISC_DONT failed wsa={}",
                     wsa_error);
      return;
    }
  }
#  endif

  created = true;
  socket_ = sock;
}

WinUdpSocket::~WinUdpSocket() = default;

ISocket& WinUdpSocket::Connect(AddressPort const& destination,
                               ConnectedCb connected_cb) {
  assert(socket_ != INVALID_SOCKET);

  ConnectionState connection_state{ConnectionState::kNone};

  ae_defer[&]() {
    Poll();
    connected_cb(connection_state);
  };

  {
    auto lock = std::scoped_lock{socket_lock_};

    auto addr = GetSockAddr(destination);
    auto res =
        connect(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (res == SOCKET_ERROR) {
      AE_TELED_ERROR("Socket connect error {}", WSAGetLastError());
      connection_state = ConnectionState::kConnectionFailed;
      return *this;
    }
  }

  if (!RequestRecv()) {
    AE_TELED_ERROR("Request receive failed!");
    connection_state = ConnectionState::kConnectionFailed;
    return *this;
  }

  connection_state = ConnectionState::kConnected;
  return *this;
}
}  // namespace ae
#endif
