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

#include "send_messages_bandwidth/common/sender.h"

#include "aether/api_protocol/packet_builder.h"
#include "aether/client_messages/p2p_message_stream.h"

#include "aether/tele/tele.h"

namespace ae::bench {
Sender::Sender(ActionContext action_context, Client::ptr client,
               Uid destination)
    : action_context_{action_context},
      client_{std::move(client)},
      destination_{destination},
      response_read_{protocol_context_, BandwidthApi{}} {}

EventSubscriber<void()> Sender::error_event() { return error_event_; }

void Sender::Connect() {
  message_stream_ =
      MakePtr<P2pStream>(action_context_, client_, destination_, StreamId{0});
  Tie(response_read_, *message_stream_);
}

void Sender::Disconnect() { message_stream_.Reset(); }

EventSubscriber<void()> Sender::Handshake() {
  auto handshake_request = RequestId::GenRequestId();
  AE_TELED_DEBUG("Sending handshake request {}", handshake_request);
  protocol_context_.AddSendResultCallback(
      handshake_request, [this](ApiParser& parser) {
        AE_TELED_DEBUG("Handshake received");
        if (parser.Extract<bool>()) {
          handshake_made_.Emit();
        } else {
          error_event_.Emit();
        }
      });

  message_stream_->in().Write(
      PacketBuilder{protocol_context_, PackMessage{BandwidthApi{},
                                                   BandwidthApi::Handshake{
                                                       {}, handshake_request}}},
      Now());

  return handshake_made_;
}

EventSubscriber<void()> Sender::Sync() {
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

EventSubscriber<void(Bandwidth const&)> Sender::WarmUp(
    std::size_t message_count) {
  test_subscriptions_.Reset();

  warm_up_ = CreateTestAction<BandwidthApi::WarmUp>(message_count);
  test_subscriptions_.Push(
      warm_up_->FinishedEvent().Subscribe([this]() { warm_up_.Reset(); }));
  return test_finished_event_;
}

EventSubscriber<void(Bandwidth const&)> Sender::OneByte(
    std::size_t message_count) {
  test_subscriptions_.Reset();

  one_byte_ = CreateTestAction<BandwidthApi::OneByte>(message_count);
  test_subscriptions_.Push(
      one_byte_->FinishedEvent().Subscribe([this]() { one_byte_.Reset(); }));
  return test_finished_event_;
}

EventSubscriber<void(Bandwidth const&)> Sender::TenBytes(
    std::size_t message_count) {
  test_subscriptions_.Reset();

  ten_bytes_ = CreateTestAction<BandwidthApi::TenBytes>(message_count);
  test_subscriptions_.Push(
      ten_bytes_->FinishedEvent().Subscribe([this]() { ten_bytes_.Reset(); }));
  return test_finished_event_;
}

EventSubscriber<void(Bandwidth const&)> Sender::HundredBytes(
    std::size_t message_count) {
  test_subscriptions_.Reset();
  hundred_bytes_ = CreateTestAction<BandwidthApi::HundredBytes>(message_count);
  test_subscriptions_.Push(hundred_bytes_->FinishedEvent().Subscribe(
      [this]() { hundred_bytes_.Reset(); }));
  return test_finished_event_;
}

EventSubscriber<void(Bandwidth const&)> Sender::ThousandBytes(
    std::size_t message_count) {
  test_subscriptions_.Reset();

  thousand_bytes_ =
      CreateTestAction<BandwidthApi::ThousandBytes>(message_count);
  test_subscriptions_.Push(thousand_bytes_->FinishedEvent().Subscribe(
      [this]() { thousand_bytes_.Reset(); }));
  return test_finished_event_;
}

EventSubscriber<void(Bandwidth const&)> Sender::VariableSizeBytes(
    std::size_t /* size */, std::size_t message_count) {
  test_subscriptions_.Reset();

  variable_size_ =
      CreateTestAction<BandwidthApi::VarMessageSize>(message_count);
  test_subscriptions_.Push(variable_size_->FinishedEvent().Subscribe(
      [this]() { variable_size_.Reset(); }));
  return test_finished_event_;
}

template <typename T>
Ptr<MessageSender<BandwidthApi, T>> Sender::CreateTestAction(
    std::size_t message_count) {
  auto action = MakePtr<MessageSender<BandwidthApi, T>>(
      action_context_, protocol_context_, BandwidthApi{}, message_stream_,
      message_count);

  test_subscriptions_.Push(
      action->SubscribeOnResult([this](auto const& action) {
        test_finished_event_.Emit(
            {action.send_duration(), action.message_send_count(), sizeof(T)});
      }),
      action->SubscribeOnError([this](auto const&) { error_event_.Emit(); }),
      action->SubscribeOnStop([this](auto const&) { error_event_.Emit(); }));

  return action;
}

}  // namespace ae::bench
