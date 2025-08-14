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
#include "aether/events/events.h"
#include "aether/stream_api/istream.h"
#include "aether/actions/action_context.h"
#include "aether/actions/repeatable_task.h"
#include "aether/events/event_subscription.h"
#include "aether/events/multi_subscription.h"

#include "send_messages_bandwidth/common/bandwidth.h"
#include "send_messages_bandwidth/common/bandwidth_api.h"

#include "send_messages_bandwidth/sender/message_sender.h"

namespace ae::bench {
class Sender {
 public:
  Sender(ActionContext action_context, Client::ptr client, Uid destination);

  EventSubscriber<void()> error_event();

  void Connect();
  void Disconnect();
  EventSubscriber<void()> Handshake();

  EventSubscriber<void(Bandwidth const&)> TestMessages(
      std::size_t message_count, std::size_t message_size);

 private:
  EventSubscriber<void()> StartTest();
  EventSubscriber<void()> StopTest();

  void OnRecvData(DataBuffer const& data);

  ActionContext action_context_;
  Client::ptr client_;
  Uid destination_;
  ProtocolContext protocol_context_;
  BandwidthApi bandwidth_api_;

  Event<void(Bandwidth const&)> test_finished_event_;
  Event<void()> handshake_made_;
  Event<void()> test_started_event_;
  Event<void()> test_stopped_event_;
  Event<void()> error_event_;

  std::unique_ptr<ByteIStream> message_stream_;

  OwnActionPtr<RepeatableTask> start_test_action_;
  OwnActionPtr<RepeatableTask> stop_test_action_;
  OwnActionPtr<MessageSender> message_sender_;

  Subscription on_recv_data_sub_;
  Subscription handshake_sub_;
  MultiSubscription sync_subs_;
  Subscription sync_action_failed_sub_;
  Subscription test_success_sub_;
  Subscription test_error_sub_;
  Subscription start_test_sub_;
  Subscription stop_test_sub_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_SENDER_H_
