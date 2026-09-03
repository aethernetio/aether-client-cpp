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

#include "aether/transport/browser/browser_websocket_transport.h"

#include <cassert>
#include <chrono>
#include <limits>
#include <utility>

#include "aether/tele.h"
#include "aether/transport/browser/browser_endpoint.h"
#include "aether/transport/browser/browser_net_api.h"
#include "aether/transport/transport_tele.h"

namespace ae {
namespace {
using browser_transport_internal::EmplaceFailedWrite;
using browser_transport_internal::FrameLengthPrefixedPacket;
using browser_transport_internal::GenerationGuard;
using browser_transport_internal::kBrowserWsBufferedAmountHighWater;
using browser_transport_internal::NativeBrowserNetHooks;
}  // namespace

BrowserWebSocketTransport::BrowserWebSocketTransport(
    AeContext const& ae_context, Endpoint endpoint)
    : ae_context_{ae_context},
      endpoint_{std::move(endpoint)},
      url_{BuildWebSocketUrl(endpoint_)},
      lifetime_token_{std::make_shared<int>(0)},
      queue_manager_{ae_context_} {
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable = true;
  stream_info_.is_writable = false;
  stream_info_.max_element_size = std::numeric_limits<std::uint32_t>::max();
  stream_info_.rec_element_size = 1500;

  AE_TELED_INFO("BrowserWebSocketTransport connect {}", url_);
  Connect();
}

BrowserWebSocketTransport::~BrowserWebSocketTransport() {
  generation_.Bump();
  lifetime_token_.reset();
  CloseSocket();
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_writable = false;
}

WriteAction& BrowserWebSocketTransport::Write(DataBuffer&& in_data) {
  assert(!in_data.empty());

  auto frame = FrameLengthPrefixedPacket(std::move(in_data));
  auto* action = queue_manager_.AddPacket(
      ae_context_, std::move(frame),
      WriteActionType::SendHook{[this](DataBuffer const& data, bool* wait,
                                       bool* async, WriteActionType* self) {
        return TrySendFrame(data, wait, async, self);
      }});
  if (action == nullptr) {
    AE_TELED_ERROR("Browser WS write queue full");
    return FailedWrite();
  }

  send_action_subs_ += action->status_event().Subscribe([this](auto status) {
    if (status == WriteAction::Status::kFail) {
      AE_TELED_ERROR("Browser WS send failed");
      SetLinkState(LinkState::kLinkError);
      CloseSocket();
    }
  });
  return *action;
}

BrowserWebSocketTransport::StreamUpdateEvent::Subscriber
BrowserWebSocketTransport::stream_update_event() {
  return EventSubscriber{stream_update_event_};
}

StreamInfo BrowserWebSocketTransport::stream_info() const {
  return stream_info_;
}

BrowserWebSocketTransport::OutDataEvent::Subscriber
BrowserWebSocketTransport::out_data_event() {
  return EventSubscriber{out_data_event_};
}

void BrowserWebSocketTransport::Restream() {
  AE_TELED_DEBUG("BrowserWebSocketTransport Restream");
  generation_.Bump();
  CloseSocket();
  FailPendingWrites();
  SetLinkState(LinkState::kUnlinked);
  Connect();
}

void BrowserWebSocketTransport::Connect() {
  auto const gen = generation_.current();
#ifdef __EMSCRIPTEN__
  ws_handle_ = ae_browser_ws_open(url_.c_str(), this,
                                  static_cast<std::uint32_t>(gen),
                                  &StaticOnOpen, &StaticOnMessage,
                                  &StaticOnClose, &StaticOnError);
  if (ws_handle_ <= 0) {
    SetLinkState(LinkState::kLinkError);
  }
#else
  auto& hooks = NativeBrowserNetHooks();
  if (hooks.on_ws_connect) {
    hooks.on_ws_connect(url_);
    OnOpen(gen);
  } else {
    SetLinkState(LinkState::kLinkError);
  }
#endif
}

void BrowserWebSocketTransport::CloseSocket() {
#ifdef __EMSCRIPTEN__
  if (ws_handle_ > 0) {
    ae_browser_ws_close(ws_handle_);
  }
#else
  auto& hooks = NativeBrowserNetHooks();
  if (hooks.on_ws_close) {
    hooks.on_ws_close();
  }
#endif
  ws_handle_ = -1;
  waiting_for_drain_ = false;
  drain_action_ = nullptr;
  stream_info_.is_writable = false;
}

void BrowserWebSocketTransport::SetLinkState(LinkState state) {
  stream_info_.link_state = state;
  stream_info_.is_writable = (state == LinkState::kLinked);
  EmitStreamUpdate();
}

void BrowserWebSocketTransport::EmitStreamUpdate() {
  stream_update_sub_ =
      ae_context_.scheduler().Task([this]() { stream_update_event_.Emit(); });
}

void BrowserWebSocketTransport::FailPendingWrites() {
  // PacketQueueManager entries complete via FailedWrite on next Write when
  // closed; active drain marked fail here.
  if (drain_action_ != nullptr && !drain_action_->is_done()) {
    drain_action_->CompleteFail();
  }
  drain_action_ = nullptr;
  waiting_for_drain_ = false;
}

WriteAction& BrowserWebSocketTransport::FailedWrite() {
  return EmplaceFailedWrite(ae_context_, failed_write_);
}

bool BrowserWebSocketTransport::TrySendFrame(DataBuffer const& frame,
                                             bool* wait, bool* async,
                                             WriteActionType* self) {
  assert(wait != nullptr);
  assert(async != nullptr);
  *wait = false;
  *async = false;
  drain_action_ = self;

  if (stream_info_.link_state != LinkState::kLinked) {
    // Early write: keep queued until linked; PacketQueueManager re-enqueues.
    *wait = true;
    return true;
  }

#ifdef __EMSCRIPTEN__
  if (ws_handle_ <= 0) {
    return false;
  }
  auto const buffered = static_cast<std::size_t>(
      ae_browser_ws_buffered_amount(ws_handle_));
  if (buffered > kBrowserWsBufferedAmountHighWater) {
    *wait = true;
    ScheduleBufferedAmountPoll();
    return true;
  }

  buffered_before_send_ = buffered;
  last_send_size_ = frame.size();
  auto const sent =
      ae_browser_ws_send(ws_handle_, frame.data(),
                         static_cast<int>(frame.size()));
  if (sent < 0) {
    return false;
  }

  auto const after = static_cast<std::size_t>(
      ae_browser_ws_buffered_amount(ws_handle_));
  // Success only once our bytes have left (or nearly left) the JS send buffer.
  if (after > buffered_before_send_) {
    waiting_for_drain_ = true;
    *async = true;
    ScheduleBufferedAmountPoll();
    return true;
  }
  return true;
#else
  auto& hooks = NativeBrowserNetHooks();
  if (hooks.on_ws_send) {
    return hooks.on_ws_send(frame);
  }
  return false;
#endif
}

void BrowserWebSocketTransport::ScheduleBufferedAmountPoll() {
#ifdef __EMSCRIPTEN__
  if (buffered_poll_sub_) {
    return;
  }
  buffered_poll_sub_ = ae_context_.scheduler().DelayedTask(
      [this]() {
        buffered_poll_sub_.Reset();
        if (ws_handle_ <= 0 ||
            stream_info_.link_state != LinkState::kLinked) {
          waiting_for_drain_ = false;
          return;
        }
        auto const buffered = static_cast<std::size_t>(
            ae_browser_ws_buffered_amount(ws_handle_));
        if (waiting_for_drain_) {
          if (buffered <= buffered_before_send_) {
            waiting_for_drain_ = false;
            if (drain_action_ != nullptr && !drain_action_->is_done()) {
              drain_action_->CompleteSuccess();
            }
            drain_action_ = nullptr;
          } else if (buffered > kBrowserWsBufferedAmountHighWater) {
            ScheduleBufferedAmountPoll();
            return;
          } else {
            // Below HWM but not fully drained — accept success to avoid stall.
            waiting_for_drain_ = false;
            if (drain_action_ != nullptr && !drain_action_->is_done()) {
              drain_action_->CompleteSuccess();
            }
            drain_action_ = nullptr;
          }
        }
        queue_manager_.Send();
        if (waiting_for_drain_ ||
            buffered > kBrowserWsBufferedAmountHighWater) {
          ScheduleBufferedAmountPoll();
        }
      },
      std::chrono::milliseconds{16});
#else
  (void)0;
#endif
}

void BrowserWebSocketTransport::OnOpen(std::uint64_t generation) {
  if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
    return;
  }
  ae_context_.scheduler().Task([this, generation]() {
    if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
      return;
    }
    AE_TELED_INFO("Browser WS linked {}", url_);
    SetLinkState(LinkState::kLinked);
    queue_manager_.Send();
  });
}

void BrowserWebSocketTransport::OnMessage(std::uint64_t generation,
                                          std::uint8_t const* data, int size) {
  if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
    return;
  }
  if (data == nullptr || size <= 0) {
    return;
  }

  // Copy into the collector immediately (JS buffer is transient). Schedule only
  // a small drain task so GenericTask fits the wasm32 pool (32 bytes).
  {
    auto lock = std::scoped_lock{buffer_lock_};
    data_packet_collector_.AddData(data, static_cast<std::size_t>(size));
  }
  if (!read_event_pending_) {
    read_event_pending_ = true;
    read_event_sub_ = ae_context_.scheduler().Task([this, generation]() {
      if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
        read_event_pending_ = false;
        return;
      }
      auto lock = std::scoped_lock{buffer_lock_};
      read_event_pending_ = false;
      for (auto packet = data_packet_collector_.PopPacket(); !packet.empty();
           packet = data_packet_collector_.PopPacket()) {
        out_data_event_.Emit(packet);
      }
    });
  }
}

void BrowserWebSocketTransport::OnClose(std::uint64_t generation) {
  if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
    return;
  }
  ae_context_.scheduler().Task([this, generation]() {
    if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
      return;
    }
    ws_handle_ = -1;
    if (stream_info_.link_state == LinkState::kLinked) {
      SetLinkState(LinkState::kLinkError);
    }
  });
}

void BrowserWebSocketTransport::OnError(std::uint64_t generation) {
  if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
    return;
  }
  ae_context_.scheduler().Task([this, generation]() {
    if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
      return;
    }
    CloseSocket();
    SetLinkState(LinkState::kLinkError);
  });
}

void BrowserWebSocketTransport::StaticOnOpen(void* user_data, std::uint32_t generation) {
  auto* self = static_cast<BrowserWebSocketTransport*>(user_data);
  self->OnOpen(generation);
}

void BrowserWebSocketTransport::StaticOnMessage(void* user_data, std::uint32_t generation,
                                                std::uint8_t const* data,
                                                int size) {
  auto* self = static_cast<BrowserWebSocketTransport*>(user_data);
  self->OnMessage(generation, data, size);
}

void BrowserWebSocketTransport::StaticOnClose(void* user_data, std::uint32_t generation) {
  auto* self = static_cast<BrowserWebSocketTransport*>(user_data);
  self->OnClose(generation);
}

void BrowserWebSocketTransport::StaticOnError(void* user_data, std::uint32_t generation) {
  auto* self = static_cast<BrowserWebSocketTransport*>(user_data);
  self->OnError(generation);
}

}  // namespace ae
