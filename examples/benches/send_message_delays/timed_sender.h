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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIMED_SENDER_H_
#define EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIMED_SENDER_H_

#include <cstddef>
#include <utility>

#include "aether/state_machine.h"
#include "aether/actions/action.h"
#include "aether/actions/action_context.h"
#include "aether/stream_api/istream.h"
#include "aether/api_protocol/packet_builder.h"
#include "aether/api_protocol/protocol_context.h"

#include "aether/tele/tele.h"

#include "send_message_delays/time_table.h"

namespace ae::bench {
class ITimedSender : public Action<ITimedSender> {
 public:
  using Action::Action;
  using Action::operator=;

  virtual TimeTable const& message_times() const = 0;
  virtual void Sync() = 0;
};

/**
 * \brief Send message_count messages during send_duration
 * On success receive message_times to compare it with received times
 */
template <typename TApiClass, typename TMessage>
class TimedSender : public ITimedSender {
  enum class State : std::uint8_t {
    kSend,
    kWaitSync,
    kWaitInterval,
    kFinished,
    kError,
  };

  static constexpr auto kSyncTimeout = std::chrono::seconds{30};

 public:
  TimedSender(ActionContext action_context, ProtocolContext& protocol_context,
              Ptr<ByteStream> stream, std::size_t message_count,
              Duration min_send_interval)
      : ITimedSender{action_context},
        protocol_context_{protocol_context},
        stream_{std::move(stream)},
        message_count_{message_count},
        min_send_interval_{min_send_interval},
        state_{State::kSend},
        state_changed_subscription_{state_.changed_event().Subscribe(
            [this](auto) { Action::Trigger(); })} {
    AE_TELED_DEBUG("TimedSender with min send interval {} us",
                   min_send_interval_.count());
  }

  TimePoint Update(TimePoint current_time) override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kSend:
          Send(current_time);
          break;
        case State::kWaitSync:
        case State::kWaitInterval:
          break;
        case State::kFinished:
          this->Result(*this);
          break;
        case State::kError:
          this->Error(*this);
          break;
      }
    }

    if (state_.get() == State::kWaitSync) {
      return CheckSyncTimeout(current_time);
    }
    if (state_.get() == State::kWaitInterval) {
      return CheckIntervalTimeout(current_time);
    }

    return current_time;
  }

  TimeTable const& message_times() const override { return message_times_; }

  void Sync() override {
    AE_TELED_DEBUG("Sync");
    auto current_time = Now();
    if (current_time - last_send_time_ < min_send_interval_) {
      state_.Set(State::kWaitInterval);
      return;
    }
    state_.Set(State::kSend);
    AE_TELED_DEBUG("Synced");
  }

 private:
  void Send(TimePoint current_time) {
    AE_TELED_DEBUG("Send message {} ", static_cast<int>(current_id_));

    message_times_.emplace(current_id_, HighResTimePoint::clock::now());

    stream_->in().Write(
        PacketBuilder{protocol_context_,
                      PackMessage{api_class_, TMessage{current_id_}}},
        current_time);

    last_send_time_ = current_time;

    ++current_id_;
    if (current_id_ == message_count_) {
      state_.Set(State::kFinished);
      return;
    }
    state_ = State::kWaitSync;
  }

  TimePoint CheckSyncTimeout(TimePoint current_time) {
    if (current_time - last_send_time_ > kSyncTimeout) {
      AE_TELED_ERROR("Sync timeout");
      state_.Set(State::kError);
      return current_time;
    }
    return last_send_time_ + kSyncTimeout;
  }

  TimePoint CheckIntervalTimeout(TimePoint current_time) {
    if (current_time - last_send_time_ > min_send_interval_) {
      state_.Set(State::kSend);
      return current_time;
    }
    return last_send_time_ + min_send_interval_;
  }

  ProtocolContext& protocol_context_;
  Ptr<ByteStream> stream_;
  std::size_t message_count_;

  Duration min_send_interval_;
  TApiClass api_class_{};
  std::uint16_t current_id_{0};

  TimePoint last_send_time_;

  TimeTable message_times_;
  StateMachine<State> state_;
  Subscription state_changed_subscription_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIMED_SENDER_H_
