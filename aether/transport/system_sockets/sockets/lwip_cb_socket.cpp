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

#include "aether/transport/system_sockets/sockets/lwip_cb_socket.h"

#if LWIP_CB_SOCKET_ENABLED

#  include "lwip/err.h"
#  include "lwip/sockets.h"
#  include "lwip/sys.h"
#  include "lwip/netdb.h"
#  include "lwip/netif.h"

#  include "aether/tele/tele.h"

namespace ae {
LwipCBSocket::LwipCBSocket(IPoller& /*poller*/, int /*socket*/) {}

LwipCBSocket::~LwipCBSocket() { Disconnect(); }

ISocket& LwipCBSocket::ReadyToWrite(ReadyToWriteCb ready_to_write_cb) {
  ready_to_write_cb_ = std::move(ready_to_write_cb);
  return *this;
}

ISocket& LwipCBSocket::RecvData(RecvDataCb recv_data_cb) {
  recv_data_cb_ = std::move(recv_data_cb);
  return *this;
}

ISocket& LwipCBSocket::Error(ErrorCb error_cb) {
  error_cb_ = std::move(error_cb);
  return *this;
}

std::optional<std::size_t> LwipCBSocket::Send(Span<std::uint8_t> data) {
  /*auto lock = std::scoped_lock{socket_lock_};
  auto size_to_send = data.size();
  // add nosignal to prevent throw SIGPIPE and handle it manually
  int flags = MSG_NOSIGNAL;
  auto res = send(socket_, data.data(), size_to_send, flags);
  if (res == -1) {
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
      AE_TELED_ERROR("Send to socket error {} {}", errno, strerror(errno));
      return std::nullopt;
    }
    return 0;
  }
  return static_cast<std::size_t>(res);*/
  return {};
}

void LwipCBSocket::Disconnect() {
  /*auto lock = std::scoped_lock{socket_lock_};
  if (socket_ == kInvalidSocket) {
    return;
  }
  auto s = socket_;
  socket_ = kInvalidSocket;

  poller_->Remove(s);
  poller_subscription_.Reset();
  shutdown(s, SHUT_RDWR);
  if (close(s) != 0) {
    return;
  }*/
}

void LwipCBSocket::OnReadEvent() {
  // read all data
  /*while (true) {
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
  }*/
}

void LwipCBSocket::OnWriteEvent() {
  /*if (ready_to_write_cb_) {
    ready_to_write_cb_();
  }*/
}

void LwipCBSocket::OnErrorEvent() {
  /*if (error_cb_) {
    error_cb_();
  }*/
}

std::optional<std::size_t> LwipCBSocket::Receive(Span<std::uint8_t> buffer) {
  /*auto lock = std::scoped_lock{socket_lock_};
  
  auto res = recv(socket_, buffer.data(), buffer.size(), 0);
  if (res < 0) {
    // No data
    if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
      return 0;
    }
    AE_TELED_ERROR("Recv error {} {}", errno, strerror(errno));
    return std::nullopt;
  }
  // probably the socket is shutdown
  if (res == 0) {
    AE_TELED_ERROR("Recv shutdown");
    return std::nullopt;
  }
  return static_cast<std::size_t>(res);*/
  return {};
}

std::optional<int> LwipCBSocket::GetSocketError() {
  /*auto lock = std::scoped_lock{socket_lock_};
  int err{};

  socklen_t len = sizeof(len);
  if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, static_cast<void*>(&err),
                 &len) != 0) {
    AE_TELED_ERROR("Getsockopt error {}, {}", errno, strerror(errno));
    return std::nullopt;
  }
  return err;*/
  return{};
}

}  // namespace ae
#endif
