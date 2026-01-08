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

#include "aether/transport/system_sockets/sockets/win_tcp_socket.h"

#if AE_SUPPORT_TCP && defined WIN_SOCKET_ENABLED

#  include <winsock2.h>
#  include <ws2def.h>
#  include <ws2ipdef.h>
#  include <mswsock.h>

#  include "aether/misc/defer.h"
#  include "aether/transport/system_sockets/sockets/get_sock_addr.h"

#  include "aether/tele/tele.h"

namespace ae {
namespace win_socket_internal {
LPFN_CONNECTEX GetConnectEx(SOCKET socket) {
  LPFN_CONNECTEX func_ptr = nullptr;
  GUID guid = WSAID_CONNECTEX;
  DWORD bytes;
  int rc =
      WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
               &func_ptr, sizeof(func_ptr), &bytes, NULL, NULL);
  if (rc == SOCKET_ERROR || func_ptr == nullptr) {
    AE_TELED_ERROR("ConnectEx load failed: {}", WSAGetLastError());
    return nullptr;
  }
  return func_ptr;
}

}  // namespace win_socket_internal

WinTcpSocket::WinTcpSocket(IPoller& poller)
    : WinSocket{poller, 1500}, conn_overlapped_{} {
  bool created = false;

  // ::socket() sets WSA_FLAG_OVERLAPPED by default that allows us to use iocp
  auto sock = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sock == INVALID_SOCKET) {
    AE_TELED_ERROR("Got socket creation error {}", WSAGetLastError());
    return;
  }
  // close socket on error
  defer[&] {
    if (!created) {
      ::closesocket(sock);
    }
  };

  int on = 1;
  auto res1 = ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                           reinterpret_cast<char const*>(&on), sizeof(on));
  if (res1 == SOCKET_ERROR) {
    AE_TELED_ERROR("Socket connect error {}", WSAGetLastError());
    return;
  }

  // make socket non-blocking
  u_long nonblocking = 1;
  auto res2 = ::ioctlsocket(sock, FIONBIO, &nonblocking);
  if (res2 == SOCKET_ERROR) {
    AE_TELED_ERROR("Socket make non-blocking error {}", WSAGetLastError());
    return;
  }

  created = true;
  socket_ = sock;
}

WinTcpSocket::~WinTcpSocket() = default;

ISocket& WinTcpSocket::Connect(AddressPort const& destination,
                               ConnectedCb connected_cb) {
  assert(socket_ != INVALID_SOCKET);

  connected_cb_ = std::move(connected_cb);

  defer[&]() {
    Poll();
    connected_cb_(connection_state_);
  };

  auto lock = std::scoped_lock{socket_lock_};

  auto connect_ex = win_socket_internal::GetConnectEx(socket_);
  if (connect_ex == nullptr) {
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }

  // Bind socket first (required for proper IOCP connection handling)
  sockaddr_in localAddr = {};
  localAddr.sin_family = AF_INET;
  localAddr.sin_addr.s_addr = INADDR_ANY;
  localAddr.sin_port = 0;  // Let system choose port

  if (bind(socket_, reinterpret_cast<sockaddr*>(&localAddr),
           sizeof(localAddr)) == SOCKET_ERROR) {
    AE_TELED_ERROR("Socket bind error {}", WSAGetLastError());
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }

  auto addr = GetSockAddr(destination);
  if (!connect_ex(socket_, addr.addr(), static_cast<int>(addr.size), NULL, 0,
                  NULL, &conn_overlapped_)) {
    auto err_code = WSAGetLastError();
    if (err_code != WSA_IO_PENDING) {
      AE_TELED_ERROR("Socket connect error {}", err_code);
      connection_state_ = ConnectionState::kConnectionFailed;
      return *this;
    }
  }

  AE_TELED_DEBUG("Wait connection");
  connection_state_ = ConnectionState::kConnecting;
  return *this;
}

WinTcpSocket::ConnectionState WinTcpSocket::TestConnectionState() {
  auto lock = std::scoped_lock{socket_lock_};

  int error = 0;
  int opt_len = sizeof(error);
  if (auto res = getsockopt(socket_, SOL_SOCKET, SO_ERROR,
                            reinterpret_cast<char*>(&error), &opt_len);
      res == SOCKET_ERROR) {
    AE_TELED_ERROR("Getsockopt returns error {}", res);
    return ConnectionState::kConnectionFailed;
  }

  if (error != 0) {
    if ((error == WSA_IO_PENDING) || (error == WSAEINPROGRESS)) {
      AE_TELED_DEBUG("Still connecting");
      return ConnectionState::kConnecting;
    }
    AE_TELED_ERROR("Socket connect error {}", error);
    return ConnectionState::kConnectionFailed;
  }

  AE_TELED_DEBUG("Tcp connected");
  return ConnectionState::kConnected;
}

void WinTcpSocket::PollEvent(LPOVERLAPPED overlapped) {
  if (connection_state_ == ConnectionState::kConnecting) {
    defer[&]() {
      if (connected_cb_) {
        connected_cb_(connection_state_);
      }
    };

    connection_state_ = TestConnectionState();
    if (!InitConnection()) {
      AE_TELED_ERROR("Failed to initiate connection");
      connection_state_ = ConnectionState::kConnectionFailed;
    }
    return;
  }
  WinSocket::PollEvent(overlapped);
}

bool WinTcpSocket::InitConnection() {
  {
    auto lock = std::scoped_lock{socket_lock_};
    // Update socket context for proper API usage
    if (setsockopt(socket_, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) ==
        SOCKET_ERROR) {
      AE_TELED_ERROR("SO_UPDATE_CONNECT_CONTEXT failed: {}", WSAGetLastError());
      return false;
    }
  }

  if (!RequestRecv()) {
    AE_TELED_ERROR("Request receive failed!");
    return false;
  }
  return true;
}

}  // namespace ae
#endif
