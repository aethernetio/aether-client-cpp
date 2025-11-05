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

#include "aether/transport/system_sockets/sockets/win_socket.h"

#if defined WIN_SOCKET_ENABLED

#  include <winsock2.h>
#  include <ws2def.h>
#  include <ws2ipdef.h>

#  include <algorithm>

#  include "aether/transport/transport_tele.h"

namespace ae {
WinSocket::WinSocket(std::size_t max_packet_size)
    : recv_overlapped_{{}, EventType::kRead},
      send_overlapped_{{}, EventType::kWrite},
      read_buffer_(max_packet_size) {}

WinSocket::~WinSocket() = default;

WinSocket::operator DescriptorType() const { return DescriptorType{socket_}; }

std::optional<std::size_t> WinSocket::Send(Span<std::uint8_t> data) {
  DWORD bytes_transferred = 0;
  DWORD flags = 0;
  if (!is_send_pending_) {
    auto wsa_buffer = WSABUF{static_cast<ULONG>(data.size()),
                             reinterpret_cast<char*>(data.data())};
    auto res =
        WSASend(socket_, &wsa_buffer, 1, &bytes_transferred, 0,
                reinterpret_cast<OVERLAPPED*>(&send_overlapped_), nullptr);
    if (res == SOCKET_ERROR) {
      auto error_code = WSAGetLastError();
      if (error_code == WSA_IO_PENDING) {
        // pending send
        is_send_pending_ = true;
        return 0;
      } else {
        AE_TELED_ERROR("Socket send error {}", error_code);
        return std::nullopt;
      }
    }
  } else {
    auto res = WSAGetOverlappedResult(
        socket_, reinterpret_cast<OVERLAPPED*>(&send_overlapped_),
        &bytes_transferred, false, &flags);

    if (!res) {
      AE_TELED_ERROR("Send WSAGetOverlappedResult socket {} get error {}",
                     socket_, WSAGetLastError());
      return std::nullopt;
    }
  }

  return static_cast<std::size_t>(bytes_transferred);
}

std::optional<std::size_t> WinSocket::Receive(Span<std::uint8_t> data) {
  auto res = HandleRecv(data);
  RequestRecv();
  return res;
}

std::size_t WinSocket::GetMaxPacketSize() const { return read_buffer_.size(); }

bool WinSocket::IsValid() const { return socket_ != INVALID_SOCKET; }

bool WinSocket::RequestRecv() {
  DWORD flags = 0;
  // request for read
  auto wsabuf = WSABUF{static_cast<ULONG>(read_buffer_.size()),
                       reinterpret_cast<char*>(read_buffer_.data())};
  auto res = WSARecv(socket_, &wsabuf, 1, nullptr, &flags,
                     reinterpret_cast<OVERLAPPED*>(&recv_overlapped_), nullptr);
  if (res == SOCKET_ERROR) {
    auto err_code = WSAGetLastError();
    if (err_code == WSA_IO_PENDING) {
      // pending read
      is_recv_pending_ = true;
      return true;
    }
    AE_TELED_ERROR("Socket receive error {}", err_code);
    return false;
  }
  return true;
}

std::optional<std::size_t> WinSocket::HandleRecv(Span<std::uint8_t> data) {
  if (!is_recv_pending_) {
    return 0;
  }
  is_recv_pending_ = false;

  DWORD bytes_transferred = 0;
  DWORD flags = 0;

  auto res = WSAGetOverlappedResult(
      socket_, reinterpret_cast<OVERLAPPED*>(&recv_overlapped_),
      &bytes_transferred, false, &flags);
  if (!res) {
    auto error_code = WSAGetLastError();
    if (error_code == WSA_IO_INCOMPLETE) {
      is_recv_pending_ = true;
      return 0;
    }
    AE_TELED_ERROR("Receive WSAGetOverlappedResult socket {} get error {}",
                   socket_, WSAGetLastError());
    return std::nullopt;
  }

  assert(bytes_transferred <= data.size());

  std::copy_n(std::begin(read_buffer_),
              static_cast<std::size_t>(bytes_transferred), std::begin(data));

  return static_cast<std::size_t>(bytes_transferred);
}

}  // namespace ae
#endif
