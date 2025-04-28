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

#include "aether/api_protocol/packet_builder.h"
#include "aether/methods/work_server_api/authorized_api.h"

#include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
CheckAccessForSendMessage::CheckAccessForSendMessage(
    ActionContext action_context, ClientToServerStream& client_to_server_stream,
    Uid destination)
    : Action{std::move(action_context)},
      client_to_server_stream_{&client_to_server_stream},
      destination_{destination},
      repeat_count_{0},
      last_request_time_{},
      state_{State::kSendRequest},
      state_changed_{state_.changed_event().Subscribe(
          [&](auto const&) { Action::Trigger(); })} {}

TimePoint CheckAccessForSendMessage::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kSendRequest:
        SendRequest(current_time);
        break;
      case State::kWaitResponse:
        break;
      case State::kReceivedSuccess:
        Action::Result(*this);
        return current_time;
      case State::kReceivedError:
      case State::kSendError:
      case State::kTimeout:
        Action::Error(*this);
        return current_time;
    }
  }
  if (state_.get() == State::kWaitResponse) {
    return WaitResponse(current_time);
  }
  return current_time;
}

void CheckAccessForSendMessage::SendRequest(TimePoint current_time) {
  last_request_time_ = current_time;

  auto api_context = ApiContext{client_to_server_stream_->protocol_context(),
                                client_to_server_stream_->authorized_api()};
  auto check_promise = api_context->check_access_for_send_message(destination_);
  wait_check_success_ = check_promise->ResultEvent().Subscribe(
      [&](auto const&) { ResponseReceived(); });
  wait_check_error_ = check_promise->ErrorEvent().Subscribe(
      [&](auto const&) { ErrorReceived(); });

  auto send_event = client_to_server_stream_->in().Write(std::move(api_context),
                                                         current_time);
  send_error_ =
      send_event->ErrorEvent().Subscribe([&](auto const&) { SendError(); });

  state_ = State::kWaitResponse;
}

TimePoint CheckAccessForSendMessage::WaitResponse(TimePoint current_time) {
  if ((last_request_time_ + kRequestTimeout) > current_time) {
    return last_request_time_ + kRequestTimeout;
  }
  repeat_count_++;
  if (repeat_count_ >= kMaxRequestRepeatCount) {
    state_ = State::kTimeout;
  } else {
    state_ = State::kSendRequest;
  }
  return current_time;
}

void CheckAccessForSendMessage::ResponseReceived() {
  AE_TELED_DEBUG("CheckAccessForSendMessage received response - success");
  state_ = State::kReceivedSuccess;
}

void CheckAccessForSendMessage::ErrorReceived() {
  AE_TELED_DEBUG("CheckAccessForSendMessage received error");
  state_ = State::kReceivedError;
}

void CheckAccessForSendMessage::SendError() {
  AE_TELED_DEBUG("CheckAccessForSendMessage send error");
  state_ = State::kSendError;
}

}  // namespace ae
