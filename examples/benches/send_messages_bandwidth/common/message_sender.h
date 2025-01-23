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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_MESSAGE_SENDER_H_
#define EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_MESSAGE_SENDER_H_

#include "aether/common.h"
#include "aether/state_machine.h"
#include "aether/actions/action.h"
#include "aether/actions/action_context.h"
#include "aether/stream_api/istream.h"
#include "aether/events/multi_subscription.h"
#include "aether/events/event_subscription.h"
#include "aether/api_protocol/packet_builder.h"
#include "aether/api_protocol/protocol_context.h"

#include "aether/tele/tele.h"

#include "send_messages_bandwidth/common/bandwidth.h"

namespace ae::bench {
template <typename TApi, typename TMessage>
class MessageSender : public Action<MessageSender<TApi, TMessage>> {
  using SelfAction = Action<MessageSender<TApi, TMessage>>;

  enum class State : std::uint8_t {
    kSending,
    kSuccess,
    kWaitbuffer,
    kStoped,
    kError,
  };

 public:
  MessageSender(ActionContext action_context, ProtocolContext& protocol_context,
                TApi&& api_class, Ptr<ByteStream> stream,
                std::size_t send_count)
      : SelfAction{action_context},
        protocol_context_{protocol_context},
        api_class_{std::move(api_class)},
        stream_{std::move(stream)},
        send_count_{send_count},
        state_{State::kSending},
        state_changed_{state_.changed_event().Subscribe(
            [this](auto) { SelfAction::Trigger(); })} {
    AE_TELED_INFO("MessageSender created");
  }

  TimePoint Update(TimePoint current_time) override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kSending:
        case State::kWaitbuffer:
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
    if (state_.get() == State::kSending) {
      Send(current_time);
    }
    if (state_.get() == State::kWaitbuffer) {
      WaitBuffer(current_time);
    }

    return current_time;
  }

  void Stop() {
    AE_TELED_DEBUG("MessageSender Stop");
    state_.Set(State::kStoped);
  }

  std::size_t message_send_count() const { return message_send_count_; }

  Duration send_duration() const {
    return std::chrono::duration_cast<Duration>(last_send_time_ -
                                                first_send_time_);
  }

 private:
  void Send(TimePoint current_time) {
    AE_TELED_DEBUG("Sending {}", message_send_count_);

    auto packet =
        PacketBuilder{protocol_context_, PackMessage{api_class_, TMessage{}}};

    auto write_action = stream_->in().Write(std::move(packet), current_time);
    message_send_.Push(  //
        write_action->SubscribeOnResult([this](auto const&) {
          message_send_confirm_count_++;
          last_send_time_ = HighResTimePoint::clock::now();
          if (state_.get() == State::kWaitbuffer) {
            SelfAction::Trigger();
          }
        }),
        write_action->SubscribeOnError([this](auto const&) {
          AE_TELED_ERROR("Error sending message");
          state_.Set(State::kError);
        }));

    if (first_send_time_.time_since_epoch() ==
        HighResTimePoint::duration::zero()) {
      first_send_time_ = HighResTimePoint::clock::now();
    }

    AE_TELED_DEBUG("Sent message {}", message_send_count_);
    ++message_send_count_;
    if (message_send_count_ == send_count_) {
      state_.Set(State::kWaitbuffer);
    }
    SelfAction::Trigger();
  }

  void WaitBuffer(TimePoint /* current_time */) {
    if (message_send_confirm_count_ == message_send_count_) {
      AE_TELED_DEBUG("Buffer is sent");
      state_.Set(State::kSuccess);
    }
  }

  ProtocolContext& protocol_context_;
  TApi api_class_;
  Ptr<ByteStream> stream_;
  std::size_t send_count_;
  Duration send_duration_;

  HighResTimePoint first_send_time_;
  HighResTimePoint last_send_time_;
  StateMachine<State> state_;
  std::size_t message_send_count_ = 0;
  std::size_t message_send_confirm_count_ = 0;

  Subscription state_changed_;
  MultiSubscription message_send_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_MESSAGE_SENDER_H_
