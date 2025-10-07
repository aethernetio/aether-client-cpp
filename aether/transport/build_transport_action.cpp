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

#include "aether/transport/build_transport_action.h"

#include "aether/tele/tele.h"

namespace ae {
BuildTransportAction::BuildTransportAction(ActionContext action_context,
                                           Channel::ptr const& channel)
    : Action{action_context},
      channel_{channel},
      state_{State::kGetBuilders},
      state_changed_sub_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {}

UpdateStatus BuildTransportAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kGetBuilders:
        MakeBuilders();
        break;
      case State::kConnect:
        Connect();
        break;
      case State::kWaitForConnection:
        break;
      case State::kConnected:
        return UpdateStatus::Result();
      case State::kFailed:
        return UpdateStatus::Error();
      default:
        break;
    }
  }
  return {};
}

std::unique_ptr<ByteIStream> BuildTransportAction::transport() {
  assert(transport_stream_);
  return std::move(transport_stream_);
}

void BuildTransportAction::MakeBuilders() {
  auto channel_ptr = channel_.Lock();
  assert(channel_ptr);

  auto builder_action = channel_ptr->TransportBuilder();
  builders_sub_ = builder_action->StatusEvent().Subscribe(ActionHandler{
      OnResult{[this](auto& action) {
        builders_ = action.builders();
        if (builders_.empty()) {
          AE_TELED_ERROR("Got empty transport builders list");
          state_ = State::kFailed;
        }
        builder_loop_ = AsyncForLoop<std::unique_ptr<ByteIStream>>{
            [this]() { it_ = std::begin(builders_); },
            [this]() { return it_ != std::end(builders_); },
            [this]() { it_++; },
            [this]() { return (*it_)->BuildTransportStream(); }};

        state_ = State::kConnect;
      }},
      OnError{[this]() {
        AE_TELED_ERROR("Create builders failed");
        state_ = State::kFailed;
      }}});
}

void BuildTransportAction::Connect() {
  assert(!builders_.empty());
  assert(builder_loop_);

  auto transport_stream = builder_loop_->Update();
  if (!transport_stream) {
    AE_TELED_ERROR("Builders list end");
    state_ = State::kFailed;
    return;
  }

  state_ = State::kWaitForConnection;

  transport_stream_ = std::move(transport_stream);

  if (transport_stream_->stream_info().link_state == LinkState::kLinked) {
    state_ = State::kConnected;
    return;
  }

  // subscribe to connection result
  connected_sub_ = transport_stream_->stream_update_event().Subscribe(
      [this, transport{transport_stream_.get()}]() {
        // connected successfully
        switch (transport->stream_info().link_state) {
          case LinkState::kLinked:
            // stream is connected
            state_ = State::kConnected;
            break;
          case LinkState::kLinkError:
            // try new stream
            state_ = State::kConnect;
            break;
          default:
            break;
        }
      });
}
}  // namespace ae
