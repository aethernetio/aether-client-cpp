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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_SENDER_H_
#define EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_SENDER_H_

#include "aether/uid.h"
#include "aether/client.h"
#include "aether/events/multi_subscription.h"
#include "aether/actions/action_view.h"
#include "aether/actions/action_context.h"
#include "aether/stream_api/safe_stream/safe_stream_config.h"

#include "send_message_delays/timed_sender.h"

namespace ae::bench {
class Sender {
 public:
  Sender(ActionContext action_context, Client::ptr client, Uid destination_uid,
         SafeStreamConfig safe_stream_config);

  void ConnectP2pStream();
  void ConnectP2pSafeStream();
  void Disconnect();

  ActionView<ITimedSender> WarmUp(std::size_t message_count,
                                  Duration min_send_interval);
  ActionView<ITimedSender> Send2Bytes(std::size_t message_count,
                                      Duration min_send_interval);
  ActionView<ITimedSender> Send10Bytes(std::size_t message_count,
                                       Duration min_send_interval);
  ActionView<ITimedSender> Send100Bytes(std::size_t message_count,
                                        Duration min_send_interval);
  ActionView<ITimedSender> Send1000Bytes(std::size_t message_count,
                                         Duration min_send_interval);
  ActionView<ITimedSender> Send1500Bytes(std::size_t message_count,
                                         Duration min_send_interval);

 private:
  template <typename TMessage>
  void CreateBenchAction(std::size_t message_count, Duration min_send_interval);

  ActionContext action_context_;
  Client::ptr client_;
  Uid destination_uid_;
  SafeStreamConfig safe_stream_config_;
  Ptr<ByteStream> send_message_stream_;
  ProtocolContext protocol_context_;

  Ptr<ITimedSender> sender_action_;

  MultiSubscription action_subscriptions_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_SENDER_H_
