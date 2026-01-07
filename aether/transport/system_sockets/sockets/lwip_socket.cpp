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

#include "aether/transport/system_sockets/sockets/lwip_socket.h"

#if LWIP_SOCKET_ENABLED

#  include "lwip/sockets.h"

#  include "aether/tele/tele.h"

namespace ae {
LwipSocket::LwipSocket(IPoller& poller, int socket)
    : poller_{static_cast<FreeRtosLwipPollerImpl*>(poller.Native())},
      socket_{socket} {}

LwipSocket::~LwipSocket() { Disconnect(); }

ISocket& LwipSocket::ReadyToWrite(ReadyToWriteCb ready_to_write_cb) {
  ready_to_write_cb_ = std::move(ready_to_write_cb);
  return *this;
}

ISocket& LwipSocket::RecvData(RecvDataCb recv_data_cb) {
  recv_data_cb_ = std::move(recv_data_cb);
  return *this;
}

ISocket& LwipSocket::Error(ErrorCb error_cb) {
  error_cb_ = std::move(error_cb);
  return *this;
}

std::optional<std::size_t> LwipSocket::Send(Span<std::uint8_t> data) {
  auto lock = std::scoped_lock{socket_lock_};
  auto size_to_send = data.size();
  // add nosignal to prevent throw SIGPIPE and handle it manually
  int flags = MSG_NOSIGNAL;
  auto res = send(socket_, data.data(), size_to_send, flags);
  if (res == -1) {
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
      // add wait for kWrite
      poller_->Event(socket_,
                     EventType::kRead | EventType::kWrite | EventType::kError,
                     MethodPtr<&LwipSocket::OnPollerEvent>{this});
    }

    if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
      AE_TELED_ERROR("Send to socket error: {}, {}", static_cast<int>(errno),
                     strerror(errno));
      return std::nullopt;
    }
    return 0;
  }
  return static_cast<std::size_t>(res);
}

void LwipSocket::Disconnect() {
  auto lock = std::scoped_lock{socket_lock_};
  if (socket_ == kInvalidSocket) {
    return;
  }
  auto s = socket_;
  socket_ = kInvalidSocket;

  poller_->Remove(s);
  shutdown(s, SHUT_RDWR);
  if (close(s) != 0) {
    return;
  }
}

void LwipSocket::Poll() {
  poller_->Event(socket_, EventType::kRead | EventType::kError,
                 MethodPtr<&LwipSocket::OnPollerEvent>{this});
}

void LwipSocket::OnPollerEvent(EventType event) {
  AE_TELED_DEBUG("Poll event desc={},event={}", socket_, event);
  for (auto e : {EventType::kRead, EventType::kWrite, EventType::kError}) {
    if ((event & e) == 0) {
      continue;
    }
    switch (e) {
      case ae::EventType::kRead:
        OnReadEvent();
        break;
      case ae::EventType::kWrite:
        OnWriteEvent();
        break;
      case ae::EventType::kError:
        OnErrorEvent();
        break;
      default:
        break;
    }
  }
}

void LwipSocket::OnReadEvent() {
  // read all data
  while (true) {
    auto buffer = Span{recv_buffer_.data(), recv_buffer_.size()};
    auto res = Receive(buffer);
    if (!res) {
      OnErrorEvent();
      return;
    }
    if (*res == 0) {
      // No data yet
      return;
    }
    buffer = buffer.sub(0, *res);
    if (recv_data_cb_) {
      recv_data_cb_(buffer);
    }
    return;
  }
}

void LwipSocket::OnWriteEvent() {
  if (ready_to_write_cb_) {
    ready_to_write_cb_();
  }
  // update poller without kWrite
  Poll();
}

void LwipSocket::OnErrorEvent() {
  if (error_cb_) {
    error_cb_();
  }
}

std::optional<std::size_t> LwipSocket::Receive(Span<std::uint8_t> buffer) {
  auto lock = std::scoped_lock{socket_lock_};

  auto res = recv(socket_, buffer.data(), buffer.size(), 0);
  if (res < 0) {
    // No data
    if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
      return 0;
    }
    AE_TELED_ERROR("Recv error: {}, {}", static_cast<int>(errno),
                   strerror(errno));
    return std::nullopt;
  }
  // probably the socket is shutdown
  if (res == 0) {
    AE_TELED_ERROR("Recv shutdown");
    return std::nullopt;
  }
  return static_cast<std::size_t>(res);
}

std::optional<int> LwipSocket::GetSocketError() {
  auto lock = std::scoped_lock{socket_lock_};
  int err{};

  socklen_t len = sizeof(len);
  if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, static_cast<void*>(&err),
                 &len) != 0) {
    AE_TELED_ERROR("Getsockopt error: {}, {}", static_cast<int>(errno),
                   strerror(errno));
    return std::nullopt;
  }
  return err;
}

}  // namespace ae
#endif
