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

#ifndef AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_WIN_SOCKET_H_
#define AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_WIN_SOCKET_H_

#if defined _WIN32
#  define WIN_SOCKET_ENABLED 1

#  include <atomic>
#  include <cstddef>

#  include "aether/poller/win_poller.h"  // for WinPollerOverlapped
#  include "aether/types/data_buffer.h"
#  include "aether/socket_initializer.h"

#  include "aether/transport/low_level/sockets/isocket.h"

namespace ae {
class WinSocket : public ISocket {
 public:
  static constexpr auto kInvalidSocketValue =
      static_cast<DescriptorType::Socket>(~0);

  explicit WinSocket(std::size_t max_packet_size);
  ~WinSocket() override;

  explicit operator DescriptorType() const override;

  std::optional<std::size_t> Send(Span<std::uint8_t> data) override;
  std::optional<std::size_t> Receive(Span<std::uint8_t> data) override;
  std::size_t GetMaxPacketSize() const override;
  bool IsValid() const override;

 protected:
  bool RequestRecv();
  std::optional<std::size_t> HandleRecv(Span<std::uint8_t> data);

  SocketInitializer socket_initializer_;
  DescriptorType::Socket socket_ = kInvalidSocketValue;

  // READ / WRITE operation states
  WinPollerOverlapped recv_overlapped_;
  WinPollerOverlapped send_overlapped_;

  std::atomic_bool is_recv_pending_ = false;
  std::atomic_bool is_send_pending_ = false;

  DataBuffer read_buffer_;
};
}  // namespace ae
#endif

#endif  // AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_WIN_SOCKET_H_
