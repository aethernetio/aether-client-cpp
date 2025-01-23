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

#include "send_messages_bandwidth/common/receiver.h"

#include "aether/client_messages/p2p_message_stream.h"

namespace ae::bench {
Receiver::Receiver(ActionContext action_context, Client::ptr client)
    : action_context_{action_context}, client_{std::move(client)} {}

EventSubscriber<void()> Receiver::error_event() { return error_event_; }

void Receiver::Connect() {
  client_connection_ = client_->client_connection();

  message_stream_received_ = client_connection_->new_stream_event().Subscribe(
      [this](auto uid, auto stream_id, auto stream) {
        AE_TELED_DEBUG("Received message stream from {}", uid);
        message_stream_ = MakePtr<P2pStream>(action_context_, client_, uid,
                                             stream_id, std::move(stream));
        protocol_read_gate_ =
            MakePtr<ProtocolReadGate>(protocol_context_, BandwidthApi{});
        Tie(*protocol_read_gate_, *message_stream_);
      });
}

void Receiver::Disconnect() {
  client_connection_.Reset();
  protocol_read_gate_.Reset();
  message_stream_.Reset();
}

EventSubscriber<void()> Receiver::Handshake() {
  handshake_received_ =
      protocol_context_
          .OnMessage<BandwidthApi::Handshake>([this](auto const& msg) {
            auto req_id = msg.message().request_id;
            AE_TELED_DEBUG("Received handshake request {}", req_id);
            message_stream_->in().Write(
                PacketBuilder{
                    protocol_context_,
                    PackMessage{ReturnResultApi{}, SendResult{req_id, true}}},
                Now());

            handshake_made_.Emit();
          })
          .Once();

  return handshake_made_;
}

EventSubscriber<void()> Receiver::Sync() {
  test_subscriptions_.Reset();
  sync_action_.emplace(action_context_, protocol_context_, message_stream_);

  test_subscriptions_.Push(  //
      sync_action_->SubscribeOnResult(
          [this](auto const&) { sync_made_.Emit(); }),
      sync_action_->SubscribeOnError(
          [this](auto const&) { error_event_.Emit(); }),
      sync_action_->FinishedEvent().Subscribe(
          [this]() { sync_action_.reset(); }));

  return sync_made_;
}

EventSubscriber<void(Bandwidth const&)> Receiver::WarmUp(
    std::size_t message_count) {
  test_subscriptions_.Reset();

  warm_up_ = CreateTestAction<BandwidthApi::WarmUp>(message_count);
  test_subscriptions_.Push(
      warm_up_->FinishedEvent().Subscribe([this]() { warm_up_.Reset(); }));

  return test_finished_event_;
}

EventSubscriber<void(Bandwidth const&)> Receiver::OneByte(
    std::size_t message_count) {
  test_subscriptions_.Reset();

  one_byte_ = CreateTestAction<BandwidthApi::OneByte>(message_count);
  test_subscriptions_.Push(
      one_byte_->FinishedEvent().Subscribe([this]() { one_byte_.Reset(); }));

  return test_finished_event_;
}

EventSubscriber<void(Bandwidth const&)> Receiver::TenBytes(
    std::size_t message_count) {
  test_subscriptions_.Reset();

  ten_bytes_ = CreateTestAction<BandwidthApi::TenBytes>(message_count);
  test_subscriptions_.Push(
      ten_bytes_->FinishedEvent().Subscribe([this]() { ten_bytes_.Reset(); }));

  return test_finished_event_;
}

EventSubscriber<void(Bandwidth const&)> Receiver::HundredBytes(
    std::size_t message_count) {
  test_subscriptions_.Reset();

  hundred_bytes_ = CreateTestAction<BandwidthApi::HundredBytes>(message_count);
  test_subscriptions_.Push(hundred_bytes_->FinishedEvent().Subscribe(
      [this]() { hundred_bytes_.Reset(); }));

  return test_finished_event_;
}

EventSubscriber<void(Bandwidth const&)> Receiver::ThousandBytes(
    std::size_t message_count) {
  test_subscriptions_.Reset();

  thousand_bytes_ =
      CreateTestAction<BandwidthApi::ThousandBytes>(message_count);
  test_subscriptions_.Push(thousand_bytes_->FinishedEvent().Subscribe(
      [this]() { thousand_bytes_.Reset(); }));

  return test_finished_event_;
}

EventSubscriber<void(Bandwidth const&)> Receiver::VariableSizeBytes(
    std::size_t message_count) {
  test_subscriptions_.Reset();

  variable_size_ =
      CreateTestAction<BandwidthApi::VarMessageSize>(message_count);
  test_subscriptions_.Push(variable_size_->FinishedEvent().Subscribe(
      [this]() { variable_size_.Reset(); }));

  return test_finished_event_;
}

template <typename T>
Ptr<MessageReceiver<T>> Receiver::CreateTestAction(std::size_t message_count) {
  auto action = MakePtr<MessageReceiver<T>>(action_context_, protocol_context_,
                                            message_count);

  test_subscriptions_.Push(
      action->SubscribeOnResult([this](auto const& action) {
        test_finished_event_.Emit({action.receive_duration(),
                                   action.message_received_count(), sizeof(T)});
      }),
      action->SubscribeOnError([this](auto const&) { error_event_.Emit(); }),
      action->SubscribeOnStop([this](auto const&) { error_event_.Emit(); }));

  return action;
}

}  // namespace ae::bench
