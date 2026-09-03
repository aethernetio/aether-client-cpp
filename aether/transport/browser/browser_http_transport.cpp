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

#include "aether/transport/browser/browser_http_transport.h"

#include <cassert>
#include <limits>
#include <utility>

#include "aether/tele.h"
#include "aether/transport/browser/browser_endpoint.h"
#include "aether/transport/browser/browser_net_api.h"

namespace ae {
namespace {
using browser_transport_internal::EmplaceFailedWrite;
using browser_transport_internal::FrameLengthPrefixedPacket;
using browser_transport_internal::GenerationGuard;
using browser_transport_internal::NativeBrowserNetHooks;
}  // namespace

BrowserHttpTransport::BrowserHttpTransport(AeContext const& ae_context,
                                           Endpoint endpoint)
    : ae_context_{ae_context},
      endpoint_{std::move(endpoint)},
      lifetime_token_{std::make_shared<int>(0)},
      queue_manager_{ae_context_} {
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable = true;
  stream_info_.is_writable = false;
  stream_info_.max_element_size = std::numeric_limits<std::uint32_t>::max();
  stream_info_.rec_element_size = 1500;

  AE_TELED_INFO("BrowserHttpTransport connect {}", BuildHttpOrigin(endpoint_));
  Connect();
}

BrowserHttpTransport::~BrowserHttpTransport() {
  generation_.Bump();
  lifetime_token_.reset();
  CloseSession();
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_writable = false;
}

WriteAction& BrowserHttpTransport::Write(DataBuffer&& in_data) {
  assert(!in_data.empty());

  auto frame = FrameLengthPrefixedPacket(std::move(in_data));
  auto* action = queue_manager_.AddPacket(
      ae_context_, std::move(frame),
      WriteActionType::SendHook{[this](DataBuffer const& data, bool* wait,
                                       bool* async, WriteActionType* self) {
        return TrySendFrame(data, wait, async, self);
      }});
  if (action == nullptr) {
    AE_TELED_ERROR("Browser HTTP write queue full");
    return FailedWrite();
  }

  send_action_subs_ += action->status_event().Subscribe([this](auto status) {
    if (status == WriteAction::Status::kFail) {
      AE_TELED_ERROR("Browser HTTP send failed");
      SetLinkState(LinkState::kLinkError);
      CloseSession();
    }
  });
  return *action;
}

BrowserHttpTransport::StreamUpdateEvent::Subscriber
BrowserHttpTransport::stream_update_event() {
  return EventSubscriber{stream_update_event_};
}

StreamInfo BrowserHttpTransport::stream_info() const { return stream_info_; }

BrowserHttpTransport::OutDataEvent::Subscriber
BrowserHttpTransport::out_data_event() {
  return EventSubscriber{out_data_event_};
}

void BrowserHttpTransport::Restream() {
  AE_TELED_DEBUG("BrowserHttpTransport Restream");
  generation_.Bump();
  CloseSession();
  if (in_flight_action_ != nullptr && !in_flight_action_->is_done()) {
    in_flight_action_->CompleteFail();
  }
  in_flight_action_ = nullptr;
  send_in_flight_ = false;
  SetLinkState(LinkState::kUnlinked);
  Connect();
}

void BrowserHttpTransport::Connect() {
  auto const gen = generation_.current();
  auto const url = BuildHttpConnectUrl(endpoint_);
  auto const body = BuildConnectTargetJson(endpoint_);

#ifdef __EMSCRIPTEN__
  http_handle_ = ae_browser_http_connect(
      url.c_str(), body.data(), static_cast<int>(body.size()), this,
      static_cast<std::uint32_t>(gen), &StaticOnConnectOk, &StaticOnConnectErr);
  if (http_handle_ <= 0) {
    SetLinkState(LinkState::kLinkError);
  }
#else
  auto& hooks = NativeBrowserNetHooks();
  if (hooks.on_http_connect) {
    session_id_ = hooks.on_http_connect(url, body);
    if (!session_id_.empty()) {
      OnConnectOk(gen, session_id_.c_str());
    } else {
      OnConnectErr(gen);
    }
  } else {
    SetLinkState(LinkState::kLinkError);
  }
#endif
}

void BrowserHttpTransport::CloseSession() {
  receive_active_ = false;
#ifdef __EMSCRIPTEN__
  if (http_handle_ > 0) {
    std::string close_url;
    if (!session_id_.empty()) {
      close_url = BuildHttpCloseUrl(endpoint_, session_id_);
    }
    ae_browser_http_close(http_handle_,
                          close_url.empty() ? nullptr : close_url.c_str());
  }
#else
  auto& hooks = NativeBrowserNetHooks();
  if (hooks.on_http_close && !session_id_.empty()) {
    hooks.on_http_close(BuildHttpCloseUrl(endpoint_, session_id_));
  }
#endif
  http_handle_ = -1;
  session_id_.clear();
  stream_info_.is_writable = false;
}

void BrowserHttpTransport::StartReceiveLoop() {
  if (receive_active_ || session_id_.empty() ||
      stream_info_.link_state != LinkState::kLinked) {
    return;
  }
  receive_active_ = true;
  auto const gen = generation_.current();
  auto const url = BuildHttpReceiveUrl(endpoint_, session_id_);

#ifdef __EMSCRIPTEN__
  if (http_handle_ <= 0) {
    receive_active_ = false;
    return;
  }
  ae_browser_http_receive(http_handle_, url.c_str(), this,
                          static_cast<std::uint32_t>(gen),
                          &StaticOnReceiveData, &StaticOnReceiveErr);
#else
  auto& hooks = NativeBrowserNetHooks();
  if (hooks.on_http_receive) {
    auto data = hooks.on_http_receive(url);
    if (!data.empty()) {
      OnReceiveData(gen, data.data(), static_cast<int>(data.size()));
    } else {
      // Empty = long-poll timeout; restart.
      receive_active_ = false;
      StartReceiveLoop();
    }
  } else {
    receive_active_ = false;
  }
#endif
}

void BrowserHttpTransport::SetLinkState(LinkState state) {
  stream_info_.link_state = state;
  stream_info_.is_writable = (state == LinkState::kLinked);
  EmitStreamUpdate();
}

void BrowserHttpTransport::EmitStreamUpdate() {
  stream_update_sub_ =
      ae_context_.scheduler().Task([this]() { stream_update_event_.Emit(); });
}

WriteAction& BrowserHttpTransport::FailedWrite() {
  return EmplaceFailedWrite(ae_context_, failed_write_);
}

bool BrowserHttpTransport::TrySendFrame(DataBuffer const& frame, bool* wait,
                                        bool* async, WriteActionType* self) {
  assert(wait != nullptr);
  assert(async != nullptr);
  *wait = false;
  *async = false;

  if (stream_info_.link_state != LinkState::kLinked || session_id_.empty()) {
    *wait = true;
    return true;
  }
  if (send_in_flight_) {
    *wait = true;
    return true;
  }

  send_in_flight_ = true;
  in_flight_action_ = self;
  auto const gen = generation_.current();
  auto const url = BuildHttpSendUrl(endpoint_, session_id_);

#ifdef __EMSCRIPTEN__
  ae_browser_http_send(url.c_str(), frame.data(),
                       static_cast<int>(frame.size()), this,
                       static_cast<std::uint32_t>(gen), &StaticOnSendOk,
                       &StaticOnSendErr);
  *async = true;
  return true;
#else
  auto& hooks = NativeBrowserNetHooks();
  if (hooks.on_http_send) {
    bool const ok = hooks.on_http_send(url, frame);
    send_in_flight_ = false;
    in_flight_action_ = nullptr;
    return ok;
  }
  send_in_flight_ = false;
  in_flight_action_ = nullptr;
  return false;
#endif
}

void BrowserHttpTransport::OnConnectOk(std::uint64_t generation,
                                       char const* session_id) {
  if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
    return;
  }
  if (session_id == nullptr || session_id[0] == '\0') {
    ae_context_.scheduler().Task([this, generation]() {
      if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
        return;
      }
      SetLinkState(LinkState::kLinkError);
    });
    return;
  }
  // Copy session id now; schedule a small lambda (wasm32 task pool is 32 bytes).
  session_id_ = session_id;
  ae_context_.scheduler().Task([this, generation]() {
    if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
      return;
    }
    AE_TELED_INFO("Browser HTTP linked session={}", session_id_);
    SetLinkState(LinkState::kLinked);
    queue_manager_.Send();
    StartReceiveLoop();
  });
}

void BrowserHttpTransport::OnConnectErr(std::uint64_t generation) {
  if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
    return;
  }
  ae_context_.scheduler().Task([this, generation]() {
    if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
      return;
    }
    http_handle_ = -1;
    SetLinkState(LinkState::kLinkError);
  });
}

void BrowserHttpTransport::OnSendOk(std::uint64_t generation) {
  if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
    return;
  }
  ae_context_.scheduler().Task([this, generation]() {
    if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
      return;
    }
    send_in_flight_ = false;
    if (in_flight_action_ != nullptr && !in_flight_action_->is_done()) {
      in_flight_action_->CompleteSuccess();
    }
    in_flight_action_ = nullptr;
    queue_manager_.Send();
  });
}

void BrowserHttpTransport::OnSendErr(std::uint64_t generation) {
  if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
    return;
  }
  ae_context_.scheduler().Task([this, generation]() {
    if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
      return;
    }
    send_in_flight_ = false;
    if (in_flight_action_ != nullptr && !in_flight_action_->is_done()) {
      in_flight_action_->CompleteFail();
    }
    in_flight_action_ = nullptr;
    SetLinkState(LinkState::kLinkError);
    CloseSession();
  });
}

void BrowserHttpTransport::OnReceiveData(std::uint64_t generation,
                                         std::uint8_t const* data, int size) {
  if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
    return;
  }
  if (data != nullptr && size > 0) {
    auto lock = std::scoped_lock{buffer_lock_};
    data_packet_collector_.AddData(data, static_cast<std::size_t>(size));
  }
  // Small scheduled task only — do not capture DataBuffer (wasm32 pool limit).
  ae_context_.scheduler().Task([this, generation]() {
    if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
      return;
    }
    receive_active_ = false;

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

    if (stream_info_.link_state == LinkState::kLinked) {
      StartReceiveLoop();
    }
  });
}

void BrowserHttpTransport::OnReceiveErr(std::uint64_t generation) {
  if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
    return;
  }
  ae_context_.scheduler().Task([this, generation]() {
    if (!GenerationGuard::IsCurrent(generation_.shared(), generation)) {
      return;
    }
    receive_active_ = false;
    SetLinkState(LinkState::kLinkError);
    CloseSession();
  });
}

void BrowserHttpTransport::StaticOnConnectOk(void* user_data, std::uint32_t generation,
                                             char const* session_id) {
  static_cast<BrowserHttpTransport*>(user_data)->OnConnectOk(generation,
                                                              session_id);
}

void BrowserHttpTransport::StaticOnConnectErr(void* user_data, std::uint32_t generation) {
  static_cast<BrowserHttpTransport*>(user_data)->OnConnectErr(generation);
}

void BrowserHttpTransport::StaticOnSendOk(void* user_data, std::uint32_t generation) {
  static_cast<BrowserHttpTransport*>(user_data)->OnSendOk(generation);
}

void BrowserHttpTransport::StaticOnSendErr(void* user_data, std::uint32_t generation) {
  static_cast<BrowserHttpTransport*>(user_data)->OnSendErr(generation);
}

void BrowserHttpTransport::StaticOnReceiveData(void* user_data, std::uint32_t generation,
                                               std::uint8_t const* data,
                                               int size) {
  static_cast<BrowserHttpTransport*>(user_data)->OnReceiveData(generation, data,
                                                               size);
}

void BrowserHttpTransport::StaticOnReceiveErr(void* user_data, std::uint32_t generation) {
  static_cast<BrowserHttpTransport*>(user_data)->OnReceiveErr(generation);
}

}  // namespace ae
