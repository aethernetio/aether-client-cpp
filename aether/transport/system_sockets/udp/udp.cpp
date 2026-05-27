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

#include "aether/transport/system_sockets/udp/udp.h"

#if SYSTEM_SOCKET_UDP_TRANSPORT_ENABLED

namespace ae::upd_internal {

UdpBase::UdpBase(AeContext const& ae_context, AddressPort endpoint)
    : ae_context_{ae_context}, endpoint_{std::move(endpoint)}, stream_info_{} {
  AE_TELE_INFO(kUdpTransport);
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable = false;
}

UdpBase::StreamUpdateEvent::Subscriber UdpBase::stream_update_event() {
  return EventSubscriber{stream_update_event_};
}
StreamInfo UdpBase::stream_info() const { return stream_info_; }
UdpBase::OutDataEvent::Subscriber UdpBase::out_data_event() {
  return EventSubscriber{out_data_event_};
}
void UdpBase::Restream() {
  AE_TELED_DEBUG("UDP restream");
  stream_info_.link_state = LinkState::kLinkError;
  Disconnect();
}

void UdpBase::OnConnected(ISocket::ConnectionState connection_state) {
  switch (connection_state) {
    case ISocket::ConnectionState::kConnecting:
    case ISocket::ConnectionState::kNone:
    case ISocket::ConnectionState::kConnectionFailed:
    case ISocket::ConnectionState::kDisconnected: {
      // Udp must be immediately connected
      AE_TELE_ERROR(kUdpTransportConnectFailed,
                    "Failed to connect udp transport to endpoint {}",
                    endpoint_);
      stream_info_.link_state = LinkState::kLinkError;
      stream_update_event_.Emit();
      break;
    }
    case ISocket::ConnectionState::kConnected: {
      stream_info_.link_state = LinkState::kLinked;
      stream_info_.is_writable = true;
      stream_update_event_.Emit();
      break;
    }
  }
}

void UdpBase::OnRecvData(Span<std::uint8_t> data) {
  auto lock = std::scoped_lock{socket_mutex_};
  // put data into read buffers
  read_buffers_.emplace_back(std::begin(data), std::end(data));
  // if not scheduled schedule a task to emit the data
  if (!read_event_.exchange(true)) {
    read_event_sub_ = ae_context_.scheduler().Task([&]() {
      auto buffers = std::invoke([&]() {
        auto lock = std::scoped_lock{socket_mutex_};
        read_event_ = false;
        return std::move(read_buffers_);
      });

      for (auto const& d : buffers) {
        AE_TELE_DEBUG(kUdpTransportReceive, "Socket {} received data size {}",
                      endpoint_, d.size());
        out_data_event_.Emit(d);
      }
    });
  }
}

void UdpBase::OnSocketError() {
  ae_context_.scheduler().Task([&]() {
    AE_TELED_ERROR("Socket error, disconnect!");
    stream_info_.link_state = LinkState::kLinkError;
    Disconnect();
  });
}

FailedWriteAction& UdpBase::FailedWrite() {
  if (!failed_write_ || failed_write_->is_finished()) {
    failed_write_.emplace(ae_context_);
  }
  return *failed_write_;
}
}  // namespace ae::upd_internal

#endif
