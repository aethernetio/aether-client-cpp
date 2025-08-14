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

#ifndef EXAMPLES_BENCHES_RECEIVER_H_
#define EXAMPLES_BENCHES_RECEIVER_H_

#include "aether/memory.h"
#include "aether/client.h"
#include "aether/actions/action_context.h"
#include "aether/events/event_subscription.h"
#include "aether/events/multi_subscription.h"
#include "aether/stream_api/safe_stream/safe_stream_config.h"
#include "aether/client_connections/split_stream_client_connection.h"

#include "send_message_delays/timed_receiver.h"
#include "send_message_delays/api/bench_delays_api.h"

namespace ae::bench {
class Receiver {
 public:
  Receiver(ActionContext action_context, Client::ptr client,
           SafeStreamConfig safe_stream_config);

  void Connect();
  void Disconnect();

  ActionPtr<TimedReceiver> WarmUp(std::size_t message_count);
  ActionPtr<TimedReceiver> Receive2Bytes(std::size_t message_count);
  ActionPtr<TimedReceiver> Receive10Bytes(std::size_t message_count);
  ActionPtr<TimedReceiver> Receive100Bytes(std::size_t message_count);
  ActionPtr<TimedReceiver> Receive1000Bytes(std::size_t message_count);

 private:
  template <typename TEvent>
  ActionPtr<TimedReceiver> CreateBenchAction(TEvent event, std::size_t count);

  void OnRecvData(DataBuffer const& data);

  ActionContext action_context_;
  Client::ptr client_;
  SafeStreamConfig safe_stream_config_;
  std::unique_ptr<SplitStreamCloudConnection> split_stream_connection_;

  ProtocolContext protocol_context_;
  BenchDelaysApi bench_delays_api_;

  std::unique_ptr<ByteIStream> receive_message_stream_;
  ActionPtr<TimedReceiver> receiver_action_;

  Subscription message_stream_subscription_;
  Subscription recv_data_sub_;
  MultiSubscription action_subscriptions_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_RECEIVER_H_
