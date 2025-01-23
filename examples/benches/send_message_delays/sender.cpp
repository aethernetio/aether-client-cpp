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

#include "send_message_delays/sender.h"

#include <utility>

#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"

#include "aether/tele/tele.h"

#include "send_message_delays/api/bench_delays_api.h"

namespace ae::bench {
Sender::Sender(ActionContext action_context, Client::ptr client,
               Uid destination_uid, SafeStreamConfig safe_stream_config)
    : action_context_{action_context},
      client_{std::move(client)},
      destination_uid_{destination_uid},
      safe_stream_config_{safe_stream_config} {}

void Sender::ConnectP2pStream() {
  AE_TELED_DEBUG("Sender::ConnectP2pStream()");

  send_message_stream_ = MakePtr<P2pStream>(action_context_, client_,
                                            destination_uid_, StreamId{0});
}

void Sender::ConnectP2pSafeStream() {
  AE_TELED_DEBUG("Sender::ConnectP2pSafeStream()");
  send_message_stream_ =
      MakePtr<P2pSafeStream>(action_context_, safe_stream_config_,
                             MakePtr<P2pStream>(action_context_, client_,
                                                destination_uid_, StreamId{1}));
}

void Sender::Disconnect() {
  AE_TELED_DEBUG("Sender::Disconnect()");
  send_message_stream_.Reset();
}

ActionView<ITimedSender> Sender::WarmUp(std::size_t message_count,
                                        Duration min_send_interval) {
  CreateBenchAction<BenchDelaysApi::WarmUp>(message_count, min_send_interval);
  return *sender_action_;
}

ActionView<ITimedSender> Sender::Send2Bytes(std::size_t message_count,
                                            Duration min_send_interval) {
  CreateBenchAction<BenchDelaysApi::TwoByte>(message_count, min_send_interval);
  return *sender_action_;
}

ActionView<ITimedSender> Sender::Send10Bytes(std::size_t message_count,
                                             Duration min_send_interval) {
  CreateBenchAction<BenchDelaysApi::TenBytes>(message_count, min_send_interval);
  return *sender_action_;
}

ActionView<ITimedSender> Sender::Send100Bytes(std::size_t message_count,
                                              Duration min_send_interval) {
  CreateBenchAction<BenchDelaysApi::HundredBytes>(message_count,
                                                  min_send_interval);
  return *sender_action_;
}

ActionView<ITimedSender> Sender::Send1000Bytes(std::size_t message_count,
                                               Duration min_send_interval) {
  CreateBenchAction<BenchDelaysApi::ThousandBytes>(message_count,
                                                   min_send_interval);
  return *sender_action_;
}

ActionView<ITimedSender> Sender::Send1500Bytes(std::size_t message_count,
                                               Duration min_send_interval) {
  CreateBenchAction<BenchDelaysApi::ThousandAndHalfBytes>(message_count,
                                                          min_send_interval);
  return *sender_action_;
}

template <typename TMessage>
void Sender::CreateBenchAction(std::size_t message_count,
                               Duration min_send_interval) {
  sender_action_ = MakePtr<TimedSender<BenchDelaysApi, TMessage>>(
      action_context_, protocol_context_, send_message_stream_, message_count,
      min_send_interval);

  action_subscriptions_.Push(sender_action_->FinishedEvent().Subscribe(
      [this]() { sender_action_.Reset(); }));
}

}  // namespace ae::bench
