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

#ifndef AETHER_TRANSPORT_WEBSOCKET_WEBSOCKET_H_
#define AETHER_TRANSPORT_WEBSOCKET_WEBSOCKET_H_

#include "aether/config.h"
#if AE_SUPPORT_WEBSOCKET
#  include <memory>
#  include <optional>
#  include <string>

#  include "aether/actions/action_pool.h"
#  include "aether/ae_context.h"
#  include "aether/stream_api/istream.h"
#  include "aether/transport/websocket/websocket_backend.h"
#  include "aether/types/address.h"

namespace ae {
namespace websocket_internal {
class Completion final : public WriteAction {
 public:
  explicit Completion(AeContext const& context);
  void Start(bool success);
  ~Completion() override;
  void Stop() noexcept override;

 private:
  AeContext context_;
  TaskSubscription task_;
};
}  // namespace websocket_internal

// ws:// and wss:// carry the SAME PackedSize-framed Aether packets as TCP.
// WebSocket message/fragment boundaries are deliberately not API boundaries.
class WebSocketTransport final : public ByteIStream {
 public:
  static constexpr std::size_t kMaxPacketSize = 1024 * 1024;
  static constexpr std::size_t kMaxBufferedBytes = 8 * 1024 * 1024;
  static constexpr std::size_t kMaxPendingMessages = 1024;
  WebSocketTransport(AeContext const& context, Endpoint const& endpoint);
  WebSocketTransport(AeContext const& context,
                     std::unique_ptr<IWebSocketBackend> backend);
  ~WebSocketTransport() override;
  WriteAction& Write(DataBuffer&& data) override;
  StreamUpdateEvent const& stream_update_event() override;
  OutDataEvent const& out_data_event() override;
  StreamInfo stream_info() const override;
  void Restream() override;

  static std::string Url(Endpoint const& endpoint);

 private:
  void Pump();
  bool Receive(DataBuffer const& data);
  void Fail();
  AeContext context_;
  std::unique_ptr<IWebSocketBackend> backend_;
  StreamInfo info_{};
  StreamUpdateEvent update_event_;
  OutDataEvent data_event_;
  DataBuffer partial_;
  ActionPool<AeContext, websocket_internal::Completion,
             AE_TCP_PACKET_QUEUE_SIZE>
      completions_;
  TaskSubscription pump_;
  std::optional<websocket_internal::Completion> failed_write_;
  // Local cancellation token, not ownership of the transport. Event handlers
  // may synchronously release their stream or request Restream().
  std::shared_ptr<bool> active_{std::make_shared<bool>(true)};
};
}  // namespace ae
#endif
#endif
