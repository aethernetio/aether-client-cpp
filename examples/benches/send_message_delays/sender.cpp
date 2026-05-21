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
#include "aether/stream_api/api_call_adapter.h"
#include "aether/client_messages/p2p_safe_message_stream.h"

#include "aether/tele/tele.h"

#include "send_message_delays/api/bench_delays_api.h"

namespace ae::bench {
Sender::Sender(AeContext const& ae_context, Client::ptr client,
               Uid destination_uid, SafeStreamConfig safe_stream_config)
    : ae_context_{ae_context},
      client_{std::move(client)},
      destination_uid_{destination_uid},
      safe_stream_config_{safe_stream_config},
      bench_delays_api_{protocol_context_} {}

void Sender::ConnectP2pStream() {
  AE_TELED_DEBUG("Sender::ConnectP2pStream()");

  send_message_stream_ =
      client_->message_stream_manager().CreateStream(destination_uid_);
  connected_stream_ = send_message_stream_.get();
}

void Sender::ConnectP2pSafeStream() {
  AE_TELED_DEBUG("Sender::ConnectP2pSafeStream()");
  send_message_safe_stream_ = make_unique<P2pSafeStream>(
      ae_context_, safe_stream_config_,
      client_->message_stream_manager().CreateStream(destination_uid_));
  connected_stream_ = send_message_safe_stream_.get();
}

void Sender::Disconnect() {
  AE_TELED_DEBUG("Sender::Disconnect()");
  send_message_stream_.Reset();
  send_message_safe_stream_.reset();
}

TimedSender& Sender::WarmUp() {
  return CreateBenchAction([](auto& api, auto id) { api->warm_up(id, {}); });
}

TimedSender& Sender::Send2Bytes() {
  return CreateBenchAction([](auto& api, auto id) { api->two_bytes(id); });
}

TimedSender& Sender::Send10Bytes() {
  return CreateBenchAction([](auto& api, auto id) { api->ten_bytes(id, {}); });
}

TimedSender& Sender::Send100Bytes() {
  return CreateBenchAction(
      [](auto& api, auto id) { api->hundred_bytes(id, {}); });
}

TimedSender& Sender::Send1000Bytes() {
  return CreateBenchAction(
      [](auto& api, auto id) { api->thousand_bytes(id, {}); });
}

template <typename Func>
TimedSender& Sender::CreateBenchAction(Func&& func) {
  sender_action_ = std::make_unique<TimedSender>(
      ae_context_, [&, f{std::forward<Func>(func)}](std::uint16_t id) {
        auto api_context =
            ApiCallAdapter{ApiContext{bench_delays_api_}, *connected_stream_};
        f(api_context, id);
        api_context.Flush();
      });

  return *sender_action_;
}

}  // namespace ae::bench
