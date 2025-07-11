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

#include "aether/transport/actions/build_transport_action.h"

#include "aether/tele/tele.h"

namespace ae {
BuildTransportAction::BuildTransportAction(ActionContext action_context,
                                           Adapter::ptr const& adapter,
                                           Channel::ptr const& channel)
    : Action{action_context},
      adapter_{adapter},
      channel_{channel},
      state_{State::kGetBuilders},
      state_changed_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {}

ActionResult BuildTransportAction::Update() {
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
        return ActionResult::Result();
      case State::kFailed:
        return ActionResult::Error();
      default:
        break;
    }
  }
  return {};
}

std::unique_ptr<ITransport> BuildTransportAction::transport() {
  assert(transport_);
  return std::move(transport_);
}

void BuildTransportAction::MakeBuilders() {
  auto adapter_ptr = adapter_.Lock();
  assert(adapter_ptr);
  auto channel_ptr = channel_.Lock();
  assert(channel_ptr);

  auto builder_action = adapter_ptr->CreateTransport(channel_ptr->address);
  builders_created_ =
      builder_action->ResultEvent().Subscribe([this](auto& action) {
        builders_ = action.builders();
        if (builders_.empty()) {
          AE_TELED_ERROR("Got empty transport builders list");
          state_ = State::kFailed;
        }
        builder_loop_ = AsyncForLoop<std::unique_ptr<ITransport>>{
            [this]() { it_ = std::begin(builders_); },
            [this]() { return it_ != std::end(builders_); },
            [this]() { it_++; }, [this]() { return (*it_)->BuildTransport(); }};

        state_ = State::kConnect;
      });
  builders_failed_ = builder_action->ErrorEvent().Subscribe([this](auto&) {
    AE_TELED_ERROR("Create builders failed");
    state_ = State::kFailed;
  });
}

void BuildTransportAction::Connect() {
  assert(!builders_.empty());
  assert(builder_loop_);

  auto transport = builder_loop_->Update();
  if (!transport) {
    AE_TELED_ERROR("Builders list end");
    state_ = State::kFailed;
    return;
  }

  transport_ = std::move(transport);
  transport_->Connect();
  // wait connection
  transport_->ConnectionSuccess().Subscribe(
      [this]() { state_ = State::kConnected; });
  transport_->ConnectionError().Subscribe([this]() {
    // try connect again
    state_ = State::kConnect;
  });

  state_ = State::kWaitForConnection;
}
}  // namespace ae
