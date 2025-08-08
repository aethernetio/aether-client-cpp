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

#include "aether/api_protocol/api_context.h"
#include "aether/stream_api/protocol_gates.h"
#include "aether/client_messages/p2p_safe_message_stream.h"

#include "aether/tele/tele.h"

#include "send_message_delays/api/bench_delays_api.h"

namespace ae::bench {
Sender::Sender(ActionContext action_context, Client::ptr client,
               Uid destination_uid, SafeStreamConfig safe_stream_config)
    : action_context_{action_context},
      client_{std::move(client)},
      destination_uid_{destination_uid},
      safe_stream_config_{safe_stream_config},
      split_stream_connection_{make_unique<SplitStreamCloudConnection>(
          action_context_, client_, *client_->client_connection())},
      bench_delays_api_{protocol_context_} {}

void Sender::ConnectP2pStream() {
  AE_TELED_DEBUG("Sender::ConnectP2pStream()");

  send_message_stream_ =
      split_stream_connection_->CreateStream(destination_uid_, StreamId{0});
}

void Sender::ConnectP2pSafeStream() {
  AE_TELED_DEBUG("Sender::ConnectP2pSafeStream()");
  send_message_stream_ = make_unique<P2pSafeStream>(
      action_context_, safe_stream_config_,
      split_stream_connection_->CreateStream(destination_uid_, StreamId{1}));
}

void Sender::Disconnect() {
  AE_TELED_DEBUG("Sender::Disconnect()");
  send_message_stream_.reset();
}

ActionView<TimedSender> Sender::WarmUp(Duration min_send_interval) {
  return CreateBenchAction([](auto& api, auto id) { api->warm_up(id, {}); },
                           min_send_interval);
}

ActionView<TimedSender> Sender::Send2Bytes(Duration min_send_interval) {
  return CreateBenchAction([](auto& api, auto id) { api->two_bytes(id); },
                           min_send_interval);
}

ActionView<TimedSender> Sender::Send10Bytes(Duration min_send_interval) {
  return CreateBenchAction([](auto& api, auto id) { api->ten_bytes(id, {}); },
                           min_send_interval);
}

ActionView<TimedSender> Sender::Send100Bytes(Duration min_send_interval) {
  return CreateBenchAction(
      [](auto& api, auto id) { api->hundred_bytes(id, {}); },
      min_send_interval);
}

ActionView<TimedSender> Sender::Send1000Bytes(Duration min_send_interval) {
  return CreateBenchAction(
      [](auto& api, auto id) { api->thousand_bytes(id, {}); },
      min_send_interval);
}

template <typename Func>
ActionView<TimedSender> Sender::CreateBenchAction(Func&& func,

                                                  Duration min_send_interval) {
  sender_action_.emplace(
      action_context_,
      [&, f{std::forward<Func>(func)}](std::uint16_t id) {
        auto api_context =
            ApiCallAdapter{ApiContext{protocol_context_, bench_delays_api_},
                           *send_message_stream_};
        f(api_context, id);
        api_context.Flush();
      },
      min_send_interval);

  return ActionView{*sender_action_};
}

}  // namespace ae::bench
