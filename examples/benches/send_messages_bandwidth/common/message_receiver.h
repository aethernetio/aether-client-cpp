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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_MESSAGE_RECEIVER_H_
#define EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_MESSAGE_RECEIVER_H_

#include "aether/common.h"
#include "aether/state_machine.h"
#include "aether/actions/action.h"
#include "aether/actions/action_context.h"
#include "aether/events/event_subscription.h"
#include "aether/api_protocol/protocol_context.h"

#include "aether/tele/tele.h"

#include "send_messages_bandwidth/common/bandwidth.h"

namespace ae::bench {
template <typename TMessage>
class MessageReceiver : public Action<MessageReceiver<TMessage>> {
  using SelfAction = Action<MessageReceiver<TMessage>>;

  static constexpr Duration kReceiveTimeout = std::chrono::seconds(5);

  enum class State : std::uint8_t {
    kReceiving,
    kSuccess,
    kStoped,
    kError,
  };

 public:
  MessageReceiver(ActionContext action_context,
                  ProtocolContext& protocol_context, std::size_t receive_count)
      : SelfAction{action_context},
        protocol_context_{protocol_context},
        receive_count_{receive_count},
        state_{State::kReceiving},
        state_changed_{state_.changed_event().Subscribe(
            [this](auto) { SelfAction::Trigger(); })},
        message_received_{
            protocol_context_.OnMessage<TMessage>([this](auto const& message) {
              MessageReceived(message.message());
            })} {}

  TimePoint Update(TimePoint current_time) {
    if (state_.get() == State::kReceiving) {
      return CheckReceiveTimeout(current_time);
    }
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kReceiving:
          last_message_received_time_point_ = Now();
          break;
        case State::kSuccess:
          SelfAction::Result(*this);
          break;
        case State::kStoped:
          SelfAction::Stop(*this);
          break;
        case State::kError:
          SelfAction::Error(*this);
          break;
      }
    }
    return current_time;
  }

  std::size_t message_received_count() const { return message_received_count_; }

  Duration receive_duration() const {
    return std::chrono::duration_cast<Duration>(last_message_received_time_ -
                                                first_message_received_time_);
  }

 private:
  void MessageReceived(TMessage const& /* message */) {
    AE_TELED_DEBUG("Message received");
    if (first_message_received_time_.time_since_epoch() ==
        HighResTimePoint::duration::zero()) {
      first_message_received_time_ = HighResTimePoint::clock::now();
    }
    last_message_received_time_ = HighResTimePoint::clock::now();
    last_message_received_time_point_ = Now();
    ++message_received_count_;

    if (message_received_count_ == receive_count_) {
      state_.Set(State::kSuccess);
    }
  }

  TimePoint CheckReceiveTimeout(TimePoint current_time) {
    if ((current_time - last_message_received_time_point_) > kReceiveTimeout) {
      AE_TELED_ERROR("Message receive timeout");
      state_.Set(State::kError);
      return current_time;
    }
    return last_message_received_time_point_ + kReceiveTimeout;
  }

  ProtocolContext& protocol_context_;
  std::size_t receive_count_;

  StateMachine<State> state_;

  std::size_t message_received_count_ = 0;
  TimePoint last_message_received_time_point_;
  HighResTimePoint first_message_received_time_;
  HighResTimePoint last_message_received_time_;

  Subscription state_changed_;
  Subscription message_received_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_MESSAGE_RECEIVER_H_
