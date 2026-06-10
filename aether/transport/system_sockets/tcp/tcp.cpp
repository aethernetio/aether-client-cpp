/*
 * Copyright 2024 Aethernet Inc.
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

#include "aether/transport/system_sockets/tcp/tcp.h"

#if defined SYSTEM_SOCKET_TCP_TRANSPORT_ENABLED

#  include <vector>
#  include <utility>

#  include "aether/transport/transport_tele.h"

namespace ae {
namespace tcp_internal {
TcpBase::TcpBase(AeContext const& ae_context, AddressPort endpoint) noexcept
    : ae_context_{ae_context}, endpoint_{std::move(endpoint)} {
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable = true;
  // TODO: find a better value for max element size
  stream_info_.max_element_size = std::numeric_limits<std::uint32_t>::max();
}

TcpBase::StreamUpdateEvent::Subscriber TcpBase::stream_update_event() {
  return EventSubscriber{stream_update_event_};
}
StreamInfo TcpBase::stream_info() const { return stream_info_; }
TcpBase::OutDataEvent::Subscriber TcpBase::out_data_event() {
  return EventSubscriber{out_data_event_};
}

void TcpBase::Restream() {
  AE_TELED_DEBUG("TCP restream");
  stream_info_.link_state = LinkState::kLinkError;
  Disconnect();
}

void TcpBase::OnConnection(ISocket::ConnectionState connection_state) {
  switch (connection_state) {
    case ISocket::ConnectionState::kConnecting:
      break;
    case ISocket::ConnectionState::kNone:
    case ISocket::ConnectionState::kConnectionFailed:
    case ISocket::ConnectionState::kDisconnected: {
      stream_info_.link_state = LinkState::kLinkError;
      conn_state_sub_ =
          ae_context_.scheduler().Task([&]() { stream_update_event_.Emit(); });
      break;
    }
    case ISocket::ConnectionState::kConnected: {
      stream_info_.is_writable = true;
      stream_info_.link_state = LinkState::kLinked;
      conn_state_sub_ =
          ae_context_.scheduler().Task([&]() { stream_update_event_.Emit(); });
      break;
    }
  }
}

void TcpBase::OnRecvData(Span<std::uint8_t> data) {
  auto sl = std::scoped_lock{buffer_lock_};
  AE_TELED_DEBUG("Received data {}", data.size());
  data_packet_collector_.AddData(data.data(), data.size());

  // emit new data though scheduler stream
  if (!read_event_.exchange(true)) {
    read_event_sub_ = ae_context_.scheduler().Task([&]() {
      auto sl = std::scoped_lock{buffer_lock_};
      read_event_ = false;
      for (auto data = data_packet_collector_.PopPacket(); !data.empty();
           data = data_packet_collector_.PopPacket()) {
        AE_TELE_DEBUG(kTcpTransportReceive, "Socket {} received data size {}",
                      endpoint_, data.size());
        out_data_event_.Emit(data);
      }
    });
  }
}
void TcpBase::OnReadyToWrite() {}
void TcpBase::OnSocketError() {
  conn_state_sub_ = ae_context_.scheduler().Task([&]() {
    AE_TELED_ERROR("Socket error, disconnect!");
    stream_info_.link_state = LinkState::kLinkError;
    Disconnect();
  });
}

FailedWriteAction& TcpBase::FailedWrite() {
  if (!failed_write_ || failed_write_->is_finished()) {
    failed_write_.emplace(ae_context_);
  }
  return *failed_write_;
}
}  // namespace tcp_internal
}  // namespace ae
#endif
