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
WinSocket::WinSocket(IPoller& poller, std::size_t max_packet_size)
    : poller_{static_cast<IoCpPoller*>(poller.Native())},
      recv_overlapped_{},
      send_overlapped_{},
      recv_buffer_(max_packet_size) {}

WinSocket::~WinSocket() { Disconnect(); }

ISocket& WinSocket::ReadyToWrite(ReadyToWriteCb ready_to_write_cb) {
  ready_to_write_cb_ = std::move(ready_to_write_cb);
  return *this;
}

ISocket& WinSocket::RecvData(RecvDataCb recv_data_cb) {
  recv_data_cb_ = std::move(recv_data_cb);
  return *this;
}

ISocket& WinSocket::Error(ErrorCb error_cb) {
  error_cb_ = std::move(error_cb);
  return *this;
}

std::optional<std::size_t> WinSocket::Send(Span<std::uint8_t> data) {
  auto lock = std::scoped_lock{socket_lock_};

  DWORD bytes_transferred = 0;
  DWORD flags = 0;
  if (!is_send_pending_) {
    auto wsa_buffer = WSABUF{static_cast<ULONG>(data.size()),
                             reinterpret_cast<char*>(data.data())};
    auto res = WSASend(socket_, &wsa_buffer, 1, &bytes_transferred, 0,
                       &send_overlapped_, nullptr);
    if (res == SOCKET_ERROR) {
      auto error_code = WSAGetLastError();
      if (error_code == WSA_IO_PENDING) {
        // pending send
        is_send_pending_ = true;
        return 0;
      }
      // Get real error
      AE_TELED_ERROR("Socket send error {}", error_code);
      return std::nullopt;
    }
  } else {
    auto res = WSAGetOverlappedResult(socket_, &send_overlapped_,
                                      &bytes_transferred, false, &flags);
    if (!res) {
      auto error_code = WSAGetLastError();
      if (error_code == WSA_IO_INCOMPLETE) {
        // still incomplete
        return 0;
      }
      // Get real error
      AE_TELED_ERROR("Send WSAGetOverlappedResult get error {}", error_code);
      return std::nullopt;
    }
    is_send_pending_ = false;
  }

  return static_cast<std::size_t>(bytes_transferred);
}

void WinSocket::Disconnect() {
  if (socket_ == INVALID_SOCKET) {
    return;
  }

  poller_->Remove(socket_);
  auto lock = std::scoped_lock{socket_lock_};
  closesocket(socket_);
  socket_ = INVALID_SOCKET;
}

void WinSocket::Poll() {
  poller_->Add(socket_, MethodPtr<&WinSocket::PollEvent>{this});
}

void WinSocket::PollEvent(LPOVERLAPPED overlapped) {
  if (overlapped == &recv_overlapped_) {
    OnRead();
  } else if (overlapped == &send_overlapped_) {
    OnWrite();
  }
}

void WinSocket::OnRead() {
  defer[&]() {
    // request new recv after all
    RequestRecv();
  };

  auto res = HandleRecv();
  if (!res) {
    OnError();
    return;
  }
  if (*res == 0) {
    // No data yet
    return;
  }

  if (recv_data_cb_) {
    auto buffer = Span{recv_buffer_.data(), *res};
    recv_data_cb_(buffer);
  }
}

void WinSocket::OnWrite() {
  if (ready_to_write_cb_) {
    ready_to_write_cb_();
  }
}

void WinSocket::OnError() {
  if (error_cb_) {
    return error_cb_();
  }
}

bool WinSocket::RequestRecv() {
  auto lock = std::scoped_lock{socket_lock_};

  DWORD flags = 0;
  // request for read
  auto wsabuf = WSABUF{static_cast<ULONG>(recv_buffer_.size()),
                       reinterpret_cast<char*>(recv_buffer_.data())};
  auto res =
      WSARecv(socket_, &wsabuf, 1, nullptr, &flags, &recv_overlapped_, nullptr);
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

std::optional<std::size_t> WinSocket::HandleRecv() {
  if (!is_recv_pending_) {
    return 0;
  }

  auto lock = std::scoped_lock{socket_lock_};

  DWORD bytes_transferred = 0;
  DWORD flags = 0;

  auto res = WSAGetOverlappedResult(socket_, &recv_overlapped_,
                                    &bytes_transferred, false, &flags);
  if (!res) {
    auto error_code = WSAGetLastError();
    if (error_code == WSA_IO_INCOMPLETE) {
      return 0;
    }
    AE_TELED_ERROR("Receive WSAGetOverlappedResult socket {} get error {}",
                   socket_, WSAGetLastError());
    return std::nullopt;
  }

  is_recv_pending_ = false;
  return static_cast<std::size_t>(bytes_transferred);
}

}  // namespace ae
#endif
