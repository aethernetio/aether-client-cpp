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

#ifndef AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_UNIX_SOCKET_H_
#define AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_UNIX_SOCKET_H_

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
    defined(__FreeBSD__)

#  define UNIX_SOCKET_ENABLED 1

#  include <mutex>

#  include "aether/poller/poller.h"
#  include "aether/poller/unix_poller.h"
#  include "aether/types/data_buffer.h"
#  include "aether/events/event_subscription.h"
#  include "aether/transport/system_sockets/sockets/isocket.h"

namespace ae {
/**
 * \brief Base implementation of unix socket.
 */
class UnixSocket : public ISocket {
 public:
  static constexpr int kInvalidSocket = -1;

  explicit UnixSocket(IPoller& poller, int socket);
  ~UnixSocket() override;

  ISocket& ReadyToWrite(ReadyToWriteCb ready_to_write_cb) override;
  ISocket& RecvData(RecvDataCb recv_data_cb) override;
  ISocket& Error(ErrorCb error_cb) override;
  std::optional<std::size_t> Send(Span<std::uint8_t> data) override;

  void Disconnect() override;

 protected:
  void Poll();
  virtual void OnPollerEvent(EventType event);

  void OnReadEvent();
  void OnWriteEvent();
  void OnErrorEvent();

  std::optional<std::size_t> Receive(Span<std::uint8_t> buffer);

  std::optional<int> GetSocketError();

  UnixPollerImpl* poller_;
  int socket_;
  std::mutex socket_lock_;

  ReadyToWriteCb ready_to_write_cb_;
  RecvDataCb recv_data_cb_;
  ErrorCb error_cb_;

  DataBuffer recv_buffer_;
};
}  // namespace ae

#endif

#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_UNIX_SOCKET_H_
