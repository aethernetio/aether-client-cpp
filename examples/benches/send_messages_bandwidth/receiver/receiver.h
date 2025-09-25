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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_RECEIVER_RECEIVER_H_
#define EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_RECEIVER_RECEIVER_H_

#include "aether/client.h"
#include "aether/memory.h"
#include "aether/events/events.h"
#include "aether/actions/action_ptr.h"
#include "aether/actions/action_context.h"
#include "aether/events/event_subscription.h"

#include "send_messages_bandwidth/common/bandwidth.h"
#include "send_messages_bandwidth/common/bandwidth_api.h"
#include "send_messages_bandwidth/receiver/message_receiver.h"

namespace ae::bench {
class Receiver {
 public:
  Receiver(ActionContext action_context, Client::ptr client);

  EventSubscriber<void()> error_event();

  void Connect();
  void Disconnect();
  EventSubscriber<void()> Handshake();

  EventSubscriber<void(Bandwidth const&)> TestMessages(
      std::size_t message_count, std::size_t message_size);

 private:
  MessageReceiver CreateTestAction(std::size_t message_count);

  void OnRecvData(DataBuffer const& data);

  ActionContext action_context_;
  Client::ptr client_;
  ProtocolContext protocol_context_;
  BandwidthApi bandwidth_api_;

  RcPtr<P2pStream> message_stream_;

  OwnActionPtr<MessageReceiver> message_receiver_;

  Event<void(Bandwidth const&)> test_finished_event_;
  Event<void()> handshake_made_event_;
  Event<void()> sync_made_event_;
  Event<void()> error_event_;

  bool test_stopped_{true};

  Subscription message_stream_received_;
  Subscription message_received_sub_;
  Subscription handshake_received_;
  Subscription test_start_sub_;
  Subscription test_stop_sub_;
  Subscription recv_data_sub_;
  Subscription test_res_sub_;
  Subscription test_error_sub_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_RECEIVER_RECEIVER_H_
