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

#include "aether/ae_actions/check_access_for_send_message.h"

#include "aether/methods/work_server_api/authorized_api.h"

#include "aether/ae_actions/ae_actions_tele.h"  // IWYU pragma: keep

namespace ae {
CheckAccessForSendMessage::CheckAccessForSendMessage(
    ActionContext action_context, ClientToServerStream& client_to_server_stream,
    Uid destination)
    : Action{action_context},
      action_context_{action_context},
      client_to_server_stream_{&client_to_server_stream},
      destination_{destination},
      state_{State::kSendRequest},
      state_changed_sub_{state_.changed_event().Subscribe(
          [&](auto const&) { Action::Trigger(); })} {}

UpdateStatus CheckAccessForSendMessage::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kSendRequest:
        SendRequest();
        break;
      case State::kWaitResponse:
        break;
      case State::kReceivedSuccess:
        return UpdateStatus::Result();
      case State::kReceivedError:
      case State::kSendError:
      case State::kTimeout:
        return UpdateStatus::Error();
    }
  }
  return {};
}

void CheckAccessForSendMessage::SendRequest() {
  int repeat_count = client_to_server_stream_->stream_info().is_reliable
                         ? 1
                         : kMaxRequestRepeatCount;
  repeatable_task_ = ActionPtr<RepeatableTask>{
      action_context_,
      [this]() {
        auto api_adapter = client_to_server_stream_->authorized_api_adapter();
        auto check_promise =
            api_adapter->check_access_for_send_message(destination_);
        wait_check_sub_ = check_promise->StatusEvent().Subscribe(
            ActionHandler{OnResult{[&]() { ResponseReceived(); }},
                          OnError{[&]() { ErrorReceived(); }}});

        auto send_event = api_adapter.Flush();
        send_error_sub_ = send_event->StatusEvent().Subscribe(
            OnError{[&]() { SendError(); }});
      },
      kRequestTimeout, repeat_count};

  repeat_task_error_sub_ = repeatable_task_->StatusEvent().Subscribe(
      OnError{[this]() { state_ = State::kTimeout; }});

  state_ = State::kWaitResponse;
}

void CheckAccessForSendMessage::ResponseReceived() {
  AE_TELED_DEBUG("CheckAccessForSendMessage received response - success");
  if (repeatable_task_) {
    repeatable_task_->Stop();
  }
  state_ = State::kReceivedSuccess;
}

void CheckAccessForSendMessage::ErrorReceived() {
  AE_TELED_DEBUG("CheckAccessForSendMessage received error");
  if (repeatable_task_) {
    repeatable_task_->Stop();
  }
  state_ = State::kReceivedError;
}

void CheckAccessForSendMessage::SendError() {
  AE_TELED_DEBUG("CheckAccessForSendMessage send error");
  if (repeatable_task_) {
    repeatable_task_->Stop();
  }
  state_ = State::kSendError;
}

}  // namespace ae
