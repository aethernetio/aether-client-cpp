/*
 * Copyright 2026 Aethernet Inc.
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

#include "aether/transport/websocket/websocket.h"
#if AE_SUPPORT_WEBSOCKET
#  include <algorithm>
#  include <cassert>
#  include <chrono>
#  include <type_traits>

#  include "aether-miscpp/format/format.h"
#  include "aether/clock.h"
#  include "aether/types/packed_size.h"
#  include "aether/vector_buffer.h"

namespace ae {
namespace websocket_internal {
Completion::Completion(AeContext const& context)
    : WriteAction{context}, context_{context} {}
void Completion::Start(bool success) {
  auto posted = context_.scheduler().Task(task_, [this, success]() {
    SetStatus(success ? Status::kSuccess : Status::kFail);
  });
  assert(posted && "WebSocket completion task allocation failed");
}
Completion::~Completion() { context_.scheduler().Cancel(task_); }
void Completion::Stop() noexcept {
  if (is_finished()) return;
  context_.scheduler().Cancel(task_);
  SetStatus(Status::kStop);
}
}  // namespace websocket_internal

std::string WebSocketTransport::Url(Endpoint const& endpoint) {
  auto const secure = endpoint.protocol == Protocol::kWebSocketSecure;
  auto host = std::visit(
      [](auto const& address) {
        using T = std::decay_t<decltype(address)>;
        if constexpr (std::is_same_v<T, NamedAddr>) {
          return address.name;
        } else if constexpr (std::is_same_v<T, IpV6Addr>) {
          // Emit eight 16-bit groups, not sixteen individual byte groups.
          std::string result{"["};
          constexpr char hex[] = "0123456789abcdef";
          for (std::size_t i = 0; i < 16; ++i) {
            if (i != 0 && i % 2 == 0) result += ':';
            result += hex[address.ipv6_value[i] >> 4];
            result += hex[address.ipv6_value[i] & 15];
          }
          return result + ']';
        } else {
          return Format("{}.{}.{}.{}",
                        static_cast<unsigned>(address.ipv4_value[0]),
                        static_cast<unsigned>(address.ipv4_value[1]),
                        static_cast<unsigned>(address.ipv4_value[2]),
                        static_cast<unsigned>(address.ipv4_value[3]));
        }
      },
      endpoint.address);
  return std::string{secure ? "wss://" : "ws://"} + host + ':' +
         std::to_string(endpoint.port) + '/';
}

WebSocketTransport::WebSocketTransport(AeContext const& context,
                                       Endpoint const& endpoint)
    : WebSocketTransport{context, MakeWebSocketBackend(Url(endpoint))} {}

WebSocketTransport::WebSocketTransport(
    AeContext const& context, std::unique_ptr<IWebSocketBackend> backend)
    : context_{context},
      backend_{std::move(backend)},
      update_event_{context},
      data_event_{context},
      completions_{context} {
  info_.max_element_size = kMaxPacketSize;
  info_.rec_element_size = 1500;
  info_.is_reliable = true;
  info_.link_state = LinkState::kUnlinked;
  auto posted = context_.scheduler().Task(pump_, [this] { Pump(); });
  assert(posted && "WebSocket pump task allocation failed");
}
WebSocketTransport::~WebSocketTransport() {
  *active_ = false;
  context_.scheduler().Cancel(pump_);
  backend_.reset();
}
WriteAction& WebSocketTransport::Write(DataBuffer&& data) {
  // Retain an action slot before enqueueing, so every accepted write has an
  // observable completion. Queue exhaustion follows the existing SDK contract.
  if (completions_.full()) {
    if (!failed_write_ || failed_write_->is_finished()) {
      failed_write_.emplace(context_);
      failed_write_->Start(false);
    }
    return *failed_write_;
  }
  auto* completion = completions_.Create(context_);
  assert(completion && "WebSocket write action allocation failed");
  bool success = false;
  if (backend_ && info_.is_writable && !data.empty() &&
      data.size() <= kMaxPacketSize &&
      backend_->BufferedAmount() <=
          kMaxBufferedBytes - data.size() - PacketSize::kMaxWireBytes) {
    DataBuffer framed;
    VectorBuffer<PacketSize> buffer{framed};
    buffer.Write(seri::SizeWriteTag{data.size()});
    buffer.Write(seri::DataWriteTag{data.data(), data.size()});
    success = backend_->Send(framed);
  }
  completion->Start(success);
  return *completion;
}
auto WebSocketTransport::stream_update_event() -> StreamUpdateEvent const& {
  return update_event_;
}
auto WebSocketTransport::out_data_event() -> OutDataEvent const& {
  return data_event_;
}
StreamInfo WebSocketTransport::stream_info() const { return info_; }
void WebSocketTransport::Restream() {
  // The channel/server owner creates a new transport for a new connection.
  Fail();
}
void WebSocketTransport::Fail() {
  *active_ = false;
  context_.scheduler().Cancel(pump_);
  backend_.reset();
  partial_.clear();
  info_.is_writable = false;
  info_.link_state = LinkState::kLinkError;
  update_event_.Emit();
}
bool WebSocketTransport::Receive(DataBuffer const& data) {
  auto active = active_;
  // Bound allocation before parsing a peer-controlled length. Multiple packets
  // and split size headers work independently of WebSocket message boundaries.
  for (auto byte : data) {
    partial_.push_back(byte);
    auto n = PacketSize::WireBytesNeeded(partial_.data(), partial_.size());
    if (n == 0) continue;
    PacketSize size{};
    if (!size.Deserialize(partial_.data(), n)) return false;
    auto count = static_cast<std::size_t>(size);
    if (count == 0 || count > kMaxPacketSize) return false;
    if (partial_.size() == n + count) {
      DataBuffer packet(partial_.data() + n, partial_.data() + partial_.size());
      partial_.clear();
      data_event_.Emit(packet);
      if (!*active) return false;
    }
  }
  return true;
}
void WebSocketTransport::Pump() {
  auto active = active_;
  if (!backend_) {
    if (*active) Fail();
    return;
  }
  auto update = backend_->Poll();
  if (update.state == IWebSocketBackend::State::kClosed ||
      update.state == IWebSocketBackend::State::kError) {
    // A peer may close immediately after its last complete API response.
    // Deliver already received bytes before invalidating the stream.
    info_.is_writable = false;
    for (auto const& data : update.received) {
      if (!Receive(data)) break;
    }
    if (*active) Fail();
    return;
  }
  bool writable = update.state == IWebSocketBackend::State::kOpen &&
                  backend_->BufferedAmount() < kMaxBufferedBytes;
  auto linked = update.state == IWebSocketBackend::State::kOpen
                    ? LinkState::kLinked
                    : LinkState::kUnlinked;
  if (info_.link_state != linked || info_.is_writable != writable) {
    info_.link_state = linked;
    info_.is_writable = writable;
    update_event_.Emit();
    if (!*active) return;
  }
  for (auto const& data : update.received) {
    if (!Receive(data)) {
      if (*active) Fail();
      return;
    }
  }
  auto posted = context_.scheduler().DelayedTask(
      pump_, [this] { Pump(); }, Now() + std::chrono::milliseconds{5});
  assert(posted && "WebSocket pump task allocation failed");
}
}  // namespace ae
#endif
