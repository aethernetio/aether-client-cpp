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

#include "send_message_delays/receiver.h"

#include <cstdlib>
#include <utility>

#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"

#include "send_message_delays/api/bench_delays_api.h"

#include "aether/tele/tele.h"

namespace ae::bench {
Receiver::Receiver(ActionContext action_context, Client::ptr client,
                   SafeStreamConfig safe_stream_config)
    : action_context_{action_context},
      client_{std::move(client)},
      safe_stream_config_{safe_stream_config},
      split_stream_connection_{make_unique<SplitStreamCloudConnection>(
          action_context_, client_, *client_->client_connection())} {}

void Receiver::Connect() {
  AE_TELED_DEBUG("Receiver::Connect()");

  message_stream_subscription_ =
      split_stream_connection_->new_stream_event().Subscribe(
          [this]([[maybe_unused]] auto uid, auto stream_id,
                 auto message_stream) {
            switch (stream_id) {
              case 0:  // p2p stream
              {
                AE_TELED_DEBUG("Receiver::Connect with p2p stream");
                receive_message_stream_ = std::move(message_stream);
                break;
              }
              case 1:  // p2p safe stream
              {
                AE_TELED_DEBUG("Receiver::Connect with p2p safe stream");
                receive_message_stream_ = make_unique<P2pSafeStream>(
                    action_context_, safe_stream_config_,
                    std::move(message_stream));
                break;
              }
              default:  // unknown stream
                std::abort();
            }
            recv_data_sub_ =
                receive_message_stream_->out_data_event().Subscribe(
                    *this, MethodPtr<&Receiver::OnRecvData>{});
          });
}

void Receiver::Disconnect() {
  AE_TELED_DEBUG("Receiver::Disconnect()");

  receive_message_stream_.reset();
}

ActionView<ITimedReceiver> Receiver::WarmUp(std::size_t message_count) {
  CreateBenchAction<BenchDelaysApi::WarmUp>(message_count);
  return *receiver_action_;
}

ActionView<ITimedReceiver> Receiver::Receive2Bytes(std::size_t message_count) {
  CreateBenchAction<BenchDelaysApi::TwoByte>(message_count);
  return *receiver_action_;
}

ActionView<ITimedReceiver> Receiver::Receive10Bytes(std::size_t message_count) {
  CreateBenchAction<BenchDelaysApi::TenBytes>(message_count);
  return *receiver_action_;
}

ActionView<ITimedReceiver> Receiver::Receive100Bytes(
    std::size_t message_count) {
  CreateBenchAction<BenchDelaysApi::HundredBytes>(message_count);
  return *receiver_action_;
}

ActionView<ITimedReceiver> Receiver::Receive1000Bytes(
    std::size_t message_count) {
  CreateBenchAction<BenchDelaysApi::ThousandBytes>(message_count);
  return *receiver_action_;
}

ActionView<ITimedReceiver> Receiver::Receive1500Bytes(
    std::size_t message_count) {
  CreateBenchAction<BenchDelaysApi::ThousandAndHalfBytes>(message_count);
  return *receiver_action_;
}

template <typename TMessage>
void Receiver::CreateBenchAction(std::size_t count) {
  receiver_action_ = make_unique<TimedReceiver<TMessage>>(
      action_context_, protocol_context_, count);

  action_subscriptions_.Push(receiver_action_->FinishedEvent().Subscribe(
      [this]() { receiver_action_.reset(); }));
}

void Receiver::OnRecvData(DataBuffer const& data) {
  auto parser = ApiParser{protocol_context_, data};
  auto api = BenchDelaysApi{};
  parser.Parse(api);
}

}  // namespace ae::bench
