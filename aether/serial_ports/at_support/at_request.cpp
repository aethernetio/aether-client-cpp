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

#include "aether/serial_ports/at_support/at_request.h"

#include <chrono>
#include <algorithm>

#include "aether/reflect/override_func.h"
#include "aether/serial_ports/at_support/at_support.h"

#include "aether/tele/tele.h"

namespace ae {
AtRequest::WaitObserver::WaitObserver(AtDispatcher& dispatcher, Wait wait)
    : dispatcher_{&dispatcher}, wait_{std::move(wait)}, observed_{false} {
  dispatcher_->Listen(wait_.expected, this);
}

AtRequest::WaitObserver::~WaitObserver() { dispatcher_->Remove(this); }

void AtRequest::WaitObserver::Observe(AtBuffer& buffer,
                                      AtBuffer::iterator pos) {
  observed_ = true;
  auto res = wait_.handler(buffer, pos);
  observed_event_.Emit(res);
}

Event<void(bool)>::Subscriber AtRequest::WaitObserver::observed_event() {
  return EventSubscriber{observed_event_};
}

Duration AtRequest::WaitObserver::timeout() const { return wait_.timeout; }

bool AtRequest::WaitObserver::observed() const { return observed_; }

AtRequest::AtRequest(ActionContext action_context, AtDispatcher& dispatcher,
                     AtSupport& at_support, Command at_command,
                     std::vector<Wait> waits)
    : Action{action_context},
      at_support_{&at_support},
      at_command_{std::move(at_command)},
      error_observer_{dispatcher,
                      Wait{"ERROR", {}, [](auto&, auto) { return false; }}},
      start_time_{Now()},
      response_count_{0},
      state_{State::kMakeRequest} {
  for (auto&& w : std::move(waits)) {
    wait_observers_.emplace_back(dispatcher, std::move(w));
  }

  for (auto& w : wait_observers_) {
    wait_response_sub_.Push(w.observed_event().Subscribe([this](bool res) {
      // any error lead to request failure
      if (!res) {
        state_ = State::kError;
      } else {
        response_count_++;
      }
      Action::Trigger();
    }));
  }
  wait_response_sub_.Push(error_observer_.observed_event().Subscribe(
      [this](auto) { state_ = State::kError; }));
}

UpdateStatus AtRequest::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kMakeRequest:
        MakeRequest();
        break;
      case State::kWaitResponse:
        break;
      case State::kSuccess:
        return UpdateStatus::Result();
      case State::kError:
        return UpdateStatus::Error();
    }
  }
  if (state_ == State::kWaitResponse) {
    return WaitResponses(current_time);
  }
  return {};
}

ActionPtr<AtWriteAction> AtRequest::CallCommand() {
  return std::visit(ae::reflect::OverrideFunc{
                        [this](std::string const& str) {
                          return at_support_->SendATCommand(str);
                        },
                        [](CommandMaker& command) { return command(); }},
                    at_command_.command);
}

void AtRequest::MakeRequest() {
  auto action = CallCommand();
  command_sub_ = action->StatusEvent().Subscribe(ActionHandler{
      OnResult{[this]() {
        state_ = State::kWaitResponse;
        Action::Trigger();
      }},
      OnError{[this]() {
        state_ = State::kError;
        Action::Trigger();
      }},
  });
}

UpdateStatus AtRequest::WaitResponses(TimePoint current_time) {
  AE_TELED_DEBUG("Response count {}, wait observers {}", response_count_,
                 wait_observers_.size());
  if (response_count_ == wait_observers_.size()) {
    state_ = State::kSuccess;
    return UpdateStatus::Result();
  }

  Duration timeout{std::chrono::hours{8076}};
  for (auto& w : wait_observers_) {
    if (!w.observed()) {
      timeout = std::min(timeout, w.timeout());
    }
  }

  auto timeout_time = start_time_ + timeout;
  if (timeout_time <= current_time) {
    AE_TELED_ERROR("AtRequest wait response timeout");
    return UpdateStatus::Error();
  }

  return UpdateStatus::Delay(timeout_time);
}

}  // namespace ae
