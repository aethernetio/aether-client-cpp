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

#include <optional>

#include "aether/memory.h"
#include "aether/client.h"
#include "aether/types/uid.h"
#include "aether/events/multi_subscription.h"
#include "aether/actions/action_ptr.h"
#include "aether/actions/action_context.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"
#include "aether/stream_api/safe_stream/safe_stream_config.h"

#include "send_message_delays/timed_sender.h"
#include "send_message_delays/api/bench_delays_api.h"

namespace ae::bench {
class Sender {
 public:
  Sender(ActionContext action_context, std::shared_ptr<Client> client,
         Uid destination_uid, SafeStreamConfig safe_stream_config);

  void ConnectP2pStream();
  void ConnectP2pSafeStream();
  void Disconnect();

  ActionPtr<TimedSender> WarmUp(Duration min_send_interval);
  ActionPtr<TimedSender> Send2Bytes(Duration min_send_interval);
  ActionPtr<TimedSender> Send10Bytes(Duration min_send_interval);
  ActionPtr<TimedSender> Send100Bytes(Duration min_send_interval);
  ActionPtr<TimedSender> Send1000Bytes(Duration min_send_interval);

 private:
  template <typename Func>
  ActionPtr<TimedSender> CreateBenchAction(Func&& func,

                                           Duration min_send_interval);
  ActionContext action_context_;
  std::shared_ptr<Client> client_;
  Uid destination_uid_;
  SafeStreamConfig safe_stream_config_;

  RcPtr<P2pStream> send_message_stream_;
  std::unique_ptr<P2pSafeStream> send_message_safe_stream_;
  ProtocolContext protocol_context_;
  BenchDelaysApi bench_delays_api_;

  ActionPtr<TimedSender> sender_action_;

  MultiSubscription action_subscriptions_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_SENDER_H_
