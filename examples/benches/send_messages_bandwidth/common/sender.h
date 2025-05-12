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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_SENDER_H_
#define EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_SENDER_H_

#include <optional>

#include "aether/client.h"
#include "aether/memory.h"
#include "aether/actions/action_context.h"
#include "aether/events/events.h"
#include "aether/events/multi_subscription.h"
#include "aether/stream_api/istream.h"
#include "aether/stream_api/protocol_gates.h"

#include "send_messages_bandwidth/common/bandwidth.h"
#include "send_messages_bandwidth/common/sender_sync.h"
#include "send_messages_bandwidth/common/bandwidth_api.h"
#include "send_messages_bandwidth/common/message_sender.h"

namespace ae::bench {
class Sender {
 public:
  Sender(ActionContext action_context, Client::ptr client, Uid destination);

  EventSubscriber<void()> error_event();

  void Connect();
  void Disconnect();
  EventSubscriber<void()> Handshake();
  EventSubscriber<void()> Sync();

  EventSubscriber<void(Bandwidth const&)> WarmUp(std::size_t message_count);
  EventSubscriber<void(Bandwidth const&)> OneByte(std::size_t message_count);
  EventSubscriber<void(Bandwidth const&)> TenBytes(std::size_t message_count);
  EventSubscriber<void(Bandwidth const&)> HundredBytes(
      std::size_t message_count);
  EventSubscriber<void(Bandwidth const&)> ThousandBytes(
      std::size_t message_count);
  EventSubscriber<void(Bandwidth const&)> VariableSizeBytes(
      std::size_t size, std::size_t message_count);

 private:
  template <typename T>
  std::unique_ptr<MessageSender<BandwidthApi, T>> CreateTestAction(
      std::size_t message_count);

  void OnRecvData(DataBuffer const& data);

  ActionContext action_context_;
  Client::ptr client_;
  Uid destination_;
  ProtocolContext protocol_context_;

  Event<void(Bandwidth const&)> test_finished_event_;
  Event<void()> handshake_made_;
  Event<void()> sync_made_;
  Event<void()> error_event_;

  std::unique_ptr<ByteIStream> message_stream_;

  std::optional<SenderSyncAction> sync_action_;

  std::unique_ptr<MessageSender<BandwidthApi, BandwidthApi::WarmUp>> warm_up_;
  std::unique_ptr<MessageSender<BandwidthApi, BandwidthApi::OneByte>> one_byte_;
  std::unique_ptr<MessageSender<BandwidthApi, BandwidthApi::TenBytes>>
      ten_bytes_;
  std::unique_ptr<MessageSender<BandwidthApi, BandwidthApi::HundredBytes>>
      hundred_bytes_;
  std::unique_ptr<MessageSender<BandwidthApi, BandwidthApi::ThousandBytes>>
      thousand_bytes_;
  std::unique_ptr<MessageSender<BandwidthApi, BandwidthApi::VarMessageSize>>
      variable_size_;

  Subscription on_recv_data_sub_;
  MultiSubscription test_subscriptions_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_SENDER_H_
