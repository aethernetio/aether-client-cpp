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

#include "send_messages_bandwidth/sender/message_sender.h"

#include "aether/tele/tele.h"

namespace ae::bench {

MessageSender::MessageSender(ActionContext action_context, SendProc send_proc,
                             std::size_t send_count)
    : Action{action_context},
      send_proc_{std::move(send_proc)},
      send_count_{send_count},
      state_{State::kSending} {
  AE_TELED_INFO("MessageSender created");
}

UpdateStatus MessageSender::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kSending:
        first_send_time_ = HighResTimePoint::clock::now();
        break;
      case State::kWaitbuffer:
        break;
      case State::kSuccess:
        return UpdateStatus::Result();
      case State::kStopped:
        return UpdateStatus::Stop();
      case State::kError:
        return UpdateStatus::Error();
    }
  }
  if (state_.get() == State::kSending) {
    Send();
  }
  if (state_.get() == State::kWaitbuffer) {
    WaitBuffer();
  }

  return {};
}

void MessageSender::Stop() {
  AE_TELED_DEBUG("MessageSender Stop");
  state_ = State::kStopped;
  Action::Trigger();
}

std::size_t MessageSender::message_send_count() const {
  return message_send_count_;
}

Duration MessageSender::send_duration() const {
  return std::chrono::duration_cast<Duration>(last_send_time_ -
                                              first_send_time_);
}

void MessageSender::Send() {
  AE_TELED_DEBUG("Sending {}", message_send_count_);

  auto write_action = send_proc_(message_send_count_);
  message_send_.Push(  //
      write_action->StatusEvent().Subscribe(
          ActionHandler{OnResult{[this](auto const&) {
                          message_send_confirm_count_++;
                          last_send_time_ = HighResTimePoint::clock::now();
                          if (state_.get() == State::kWaitbuffer) {
                            Action::Trigger();
                          }
                        }},
                        OnError{[this](auto const&) {
                          AE_TELED_ERROR("Error sending message");
                          state_ = State::kError;
                          Action::Trigger();
                        }}}));

  ++message_send_count_;
  if (message_send_count_ == send_count_) {
    state_ = State::kWaitbuffer;
  }
  Action::Trigger();
}

void MessageSender::WaitBuffer() {
  if (message_send_confirm_count_ == message_send_count_) {
    AE_TELED_DEBUG("Buffer is sent");
    state_ = State::kSuccess;
    Action::Trigger();
  }
}

}  // namespace ae::bench
