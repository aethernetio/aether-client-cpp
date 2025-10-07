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

#include "send_messages_bandwidth/sender/sender.h"

#include "aether/api_protocol/api_context.h"
#include "aether/stream_api/protocol_gates.h"
#include "aether/client_messages/p2p_message_stream.h"

#include "aether/tele/tele.h"

namespace ae::bench {
Sender::Sender(ActionContext action_context, Client::ptr client,
               Uid destination)
    : action_context_{action_context},
      client_{std::move(client)},
      destination_{destination},
      bandwidth_api_{action_context_, protocol_context_} {}

EventSubscriber<void()> Sender::error_event() { return error_event_; }

void Sender::Connect() {
  message_stream_ =
      client_->message_stream_manager().CreateStream(destination_);

  on_recv_data_sub_ = message_stream_->out_data_event().Subscribe(
      *this, MethodPtr<&Sender::OnRecvData>{});
}

void Sender::Disconnect() { message_stream_.Reset(); }

EventSubscriber<void()> Sender::Handshake() {
  auto api = ApiCallAdapter{ApiContext{protocol_context_, bandwidth_api_},
                            *message_stream_};

  auto res = api->handshake();
  handshake_sub_ =
      res->StatusEvent().Subscribe(OnResult{[this](auto const& promise) {
        AE_TELED_DEBUG("Handshake received");
        if (promise.value()) {
          handshake_made_.Emit();
        } else {
          error_event_.Emit();
        }
      }});

  AE_TELED_DEBUG("Sending handshake request {}", res->request_id());
  api.Flush();

  return handshake_made_;
}

EventSubscriber<void(Bandwidth const&)> Sender::TestMessages(
    std::size_t message_count, std::size_t message_size) {
  // Start test
  // Make test
  // Stop test
  start_test_sub_ =
      StartTest().Subscribe([this, message_size, message_count]() {
        // test started
        auto payload_size = message_size > 2 ? message_size - 2 : 0;
        message_sender_ = OwnActionPtr<MessageSender>{
            action_context_,
            [this, payload_size](std::uint16_t id) {
              auto api =
                  ApiCallAdapter{ApiContext{protocol_context_, bandwidth_api_},
                                 *message_stream_};
              api->message(id, DataBuffer(payload_size));
              return api.Flush();
            },
            message_count};

        test_res_sub_ = message_sender_->StatusEvent().Subscribe(ActionHandler{
            OnResult{[this, message_size](auto const& result) {
              stop_test_sub_ = StopTest().Subscribe(
                  [this, bandwidth = Bandwidth{result.send_duration(),
                                               result.message_send_count(),
                                               message_size}]() {
                    test_finished_event_.Emit(bandwidth);
                  });
            }},
            OnError{[this]() { error_event_.Emit(); }}});
      });

  return test_finished_event_;
}

EventSubscriber<void()> Sender::StartTest() {
  start_test_action_ = OwnActionPtr<RepeatableTask>{
      action_context_,
      [this]() {
        AE_TELED_DEBUG("Sending start test request");
        auto api = ApiCallAdapter{ApiContext{protocol_context_, bandwidth_api_},
                                  *message_stream_};

        auto res = api->start_test();
        sync_subs_.Push(res->StatusEvent().Subscribe(OnResult{[this]() {
          start_test_action_->Stop();
          test_started_event_.Emit();
        }}));
        api.Flush();
      },
      std::chrono::seconds{1}, 5};

  sync_action_failed_sub_ = start_test_action_->StatusEvent().Subscribe(
      OnError{[this](auto const&) { error_event_.Emit(); }});

  return test_started_event_;
}

EventSubscriber<void()> Sender::StopTest() {
  stop_test_action_ = OwnActionPtr<RepeatableTask>{
      action_context_,
      [this]() {
        AE_TELED_DEBUG("Sending stop test request");
        auto api = ApiCallAdapter{ApiContext{protocol_context_, bandwidth_api_},
                                  *message_stream_};

        auto res = api->stop_test();
        sync_subs_.Push(res->StatusEvent().Subscribe(OnResult{[this]() {
          stop_test_action_->Stop();
          test_stopped_event_.Emit();
        }}));
        api.Flush();
      },
      std::chrono::seconds{1}, 5};

  return test_stopped_event_;
}

void Sender::OnRecvData(DataBuffer const& data) {
  auto parser = ApiParser{protocol_context_, data};
  parser.Parse(bandwidth_api_);
}
}  // namespace ae::bench
