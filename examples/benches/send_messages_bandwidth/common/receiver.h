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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_RECEIVER_H_
#define EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_RECEIVER_H_

#include <optional>

#include "aether/client.h"
#include "aether/memory.h"
#include "aether/actions/action_context.h"
#include "aether/events/events.h"
#include "aether/events/event_subscription.h"
#include "aether/events/multi_subscription.h"
#include "aether/client_connections/client_connection.h"

#include "send_messages_bandwidth/common/bandwidth.h"
#include "send_messages_bandwidth/common/bandwidth_api.h"
#include "send_messages_bandwidth/common/receiver_sync.h"
#include "send_messages_bandwidth/common/message_receiver.h"

namespace ae::bench {
class Receiver {
 public:
  Receiver(ActionContext action_context, Client::ptr client);

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
      std::size_t message_count);

 private:
  template <typename T>
  std::unique_ptr<MessageReceiver<T>> CreateTestAction(
      std::size_t message_count);

  void OnRecvData(DataBuffer const& data);

  ActionContext action_context_;
  Client::ptr client_;
  ProtocolContext protocol_context_;

  Ptr<ClientConnection> client_connection_;
  std::unique_ptr<ByteIStream> message_stream_;

  std::optional<ReceiverSyncAction> sync_action_;

  std::unique_ptr<MessageReceiver<BandwidthApi::WarmUp>> warm_up_;
  std::unique_ptr<MessageReceiver<BandwidthApi::OneByte>> one_byte_;
  std::unique_ptr<MessageReceiver<BandwidthApi::TenBytes>> ten_bytes_;
  std::unique_ptr<MessageReceiver<BandwidthApi::HundredBytes>> hundred_bytes_;
  std::unique_ptr<MessageReceiver<BandwidthApi::ThousandBytes>> thousand_bytes_;
  std::unique_ptr<MessageReceiver<BandwidthApi::VarMessageSize>> variable_size_;

  Event<void(Bandwidth const&)> test_finished_event_;
  Event<void()> handshake_made_;
  Event<void()> sync_made_;
  Event<void()> error_event_;

  Subscription message_stream_received_;
  Subscription handshake_received_;
  Subscription recv_data_sub_;
  MultiSubscription test_subscriptions_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_RECEIVER_H_
