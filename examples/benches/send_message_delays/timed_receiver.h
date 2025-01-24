/*
 * Copyright 2024 Aethernet Inc.
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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIMED_RECEIVER_H_
#define EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIMED_RECEIVER_H_

#include <cstddef>

#include "aether/state_machine.h"
#include "aether/events/events.h"
#include "aether/actions/action.h"
#include "aether/actions/action_context.h"
#include "aether/api_protocol/protocol_context.h"

#include "aether/tele/tele.h"

#include "send_message_delays/time_table.h"

namespace ae::bench {

class ITimedReceiver : public Action<ITimedReceiver> {
 public:
  using Action::Action;
  using Action::operator=;

  virtual TimeTable const& message_times() const = 0;
  virtual EventSubscriber<void()> OnReceived() = 0;
};

/**
 * \brief Message receiver with fixing a message time
 * Must receive a wait_count messages during wait_timeout or emit an error
 * On success receive message_times to compare it with sent times
 */
template <typename TMessage>
class TimedReceiver : public ITimedReceiver {
  enum class State : std::uint8_t {
    kWaiting,
    kReceived,
    kTimeOut,
    kError,
  };

  static constexpr auto kWaitTimeout = std::chrono::seconds{30};

 public:
  explicit TimedReceiver(ActionContext action_context,
                         ProtocolContext& protocol_context,
                         std::size_t wait_count = 3000)
      : ITimedReceiver{action_context},
        protocol_context_{protocol_context},
        wait_count_{wait_count},
        message_subscription_{protocol_context_.OnMessage<TMessage>(
            [this](auto const& msg) { OnMessage(msg.message()); })},
        state_{State::kWaiting},
        state_subscription_{state_.changed_event().Subscribe(
            [this](auto) { this->Trigger(); })} {
    AE_TELED_INFO("TimedReceiver waiting {} messages", wait_count);
  }

  TimePoint Update(TimePoint current_time) override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kWaiting:
          last_received_time_ = current_time;
          break;
        case State::kReceived:
          this->Result(*this);
          return current_time;
        case State::kTimeOut:
        case State::kError:
          this->Error(*this);
          return current_time;
      }
    }

    if (state_.get() == State::kWaiting) {
      return CheckTimeout(current_time);
    }

    return current_time;
  }

  TimeTable const& message_times() const override { return message_times_; }

  EventSubscriber<void()> OnReceived() override {
    return EventSubscriber{received_event_};
  }

 private:
  void OnMessage(TMessage const& msg) {
    AE_TELED_DEBUG("Message received id {}", static_cast<int>(msg.id));

    auto [_, ok] =
        message_times_.emplace(msg.id, HighResTimePoint::clock::now());
    if (!ok) {
      AE_TELED_ERROR("Duplicate message");
      state_.Set(State::kError);
      return;
    }

    last_received_time_ = Now();
    received_event_.Emit();

    if (message_times_.size() == wait_count_) {
      state_.Set(State::kReceived);
      return;
    }
  }

  TimePoint CheckTimeout(TimePoint current_time) {
    if ((current_time - last_received_time_) > kWaitTimeout) {
      AE_TELED_ERROR("Receive message timeout");
      state_.Set(State::kTimeOut);
      return current_time;
    }

    return last_received_time_ + kWaitTimeout;
  }

  ProtocolContext& protocol_context_;
  std::size_t wait_count_;
  Subscription message_subscription_;

  TimePoint last_received_time_;
  TimeTable message_times_;

  Event<void()> received_event_;
  StateMachine<State> state_;
  Subscription state_subscription_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIMED_RECEIVER_H_
