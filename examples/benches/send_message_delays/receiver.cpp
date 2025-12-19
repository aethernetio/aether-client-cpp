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

#include "aether/types/type_list.h"
#include "aether/client_messages/p2p_safe_message_stream.h"

#include "send_message_delays/api/bench_delays_api.h"

#include "aether/tele/tele.h"

namespace ae::bench {
Receiver::Receiver(ActionContext action_context, std::shared_ptr<Client> client,
                   SafeStreamConfig safe_stream_config)
    : action_context_{action_context},
      client_{std::move(client)},
      safe_stream_config_{safe_stream_config},
      bench_delays_api_{protocol_context_} {}

void Receiver::Connect() {
  AE_TELED_DEBUG("Receiver::Connect()");

  message_stream_subscription_ =
      client_->message_stream_manager().new_stream_event().Subscribe(
          [this](RcPtr<P2pStream> message_stream) {
            if (!receive_message_stream_) {
              receive_message_stream_ = std::move(message_stream);
              recv_data_sub_ =
                  receive_message_stream_->out_data_event().Subscribe(
                      MethodPtr<&Receiver::OnRecvData>{this});
            } else {
              receive_message_safe_stream_ = make_unique<P2pSafeStream>(
                  action_context_, safe_stream_config_,
                  std::move(message_stream));
              recv_data_sub_ =
                  receive_message_stream_->out_data_event().Subscribe(
                      MethodPtr<&Receiver::OnRecvData>{this});
            }
          });
}

void Receiver::Disconnect() { AE_TELED_DEBUG("Receiver::Disconnect()"); }

ActionPtr<TimedReceiver> Receiver::WarmUp(std::size_t message_count) {
  return CreateBenchAction(bench_delays_api_.warm_up_event(), message_count);
}

ActionPtr<TimedReceiver> Receiver::Receive2Bytes(std::size_t message_count) {
  return CreateBenchAction(bench_delays_api_.two_bytes_event(), message_count);
}

ActionPtr<TimedReceiver> Receiver::Receive10Bytes(std::size_t message_count) {
  return CreateBenchAction(bench_delays_api_.ten_bytes_event(), message_count);
}

ActionPtr<TimedReceiver> Receiver::Receive100Bytes(std::size_t message_count) {
  return CreateBenchAction(bench_delays_api_.hundred_bytes_event(),
                           message_count);
}

ActionPtr<TimedReceiver> Receiver::Receive1000Bytes(std::size_t message_count) {
  return CreateBenchAction(bench_delays_api_.thousand_bytes_event(),
                           message_count);
}

template <typename TEvent>
ActionPtr<TimedReceiver> Receiver::CreateBenchAction(TEvent event,
                                                     std::size_t count) {
  receiver_action_ = ActionPtr<TimedReceiver>{action_context_, count};
  event.Subscribe([ra{receiver_action_}](auto&&... args) mutable {
    if (ra) {
      ra->Receive(ArgAt<0>(std::forward<decltype(args)>(args)...));
    }
  });
  return receiver_action_;
}

void Receiver::OnRecvData(DataBuffer const& data) {
  auto parser = ApiParser{protocol_context_, data};
  parser.Parse(bench_delays_api_);
}

}  // namespace ae::bench
