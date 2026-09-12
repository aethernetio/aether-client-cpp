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

#include "aether/api_protocol/api_protocol.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/stream_api/api_call_adapter.h"

#include "aether/tele.h"

namespace ae::bench {
Sender::Sender(AeContext const& ae_context, Client::ptr client, Uid destination)
    : ae_context_{ae_context},
      client_{std::move(client)},
      destination_{destination},
      protocol_context_(ae_context_),
      bandwidth_api_{},
      test_finished_event_{ae_context_},
      handshake_made_{ae_context_},
      test_started_event_{ae_context_},
      test_stopped_event_{ae_context_},
      error_event_{ae_context_} {}

Event<void()> const& Sender::error_event() { return error_event_; }

void Sender::Connect() {
  auto handle = client_->message_stream_manager().CreatePort(destination_);
  message_stream_ = std::make_shared<P2pStream>(
      ae_context_, client_.Load(), destination_, std::move(handle));

  on_recv_data_sub_ = message_stream_->out_data_event().Subscribe(
      MethodPtr<&Sender::OnRecvData>{this});
}

void Sender::Disconnect() { message_stream_.reset(); }

Event<void()> const& Sender::Handshake() {
  auto api = ApiCallAdapter{bandwidth_api_, *message_stream_};

  auto res = api->Handshake();
  handshake_sub_ = res.Subscribe([this](auto const& res) {
    AE_TELED_DEBUG("Handshake received");
    if (res && res.value()) {
      handshake_made_.Emit();
    } else {
      error_event_.Emit();
    }
  });

  AE_TELED_DEBUG("Sending handshake request {}", res.request_id());
  api.Flush();

  return handshake_made_;
}

Event<void(Bandwidth const&)> const& Sender::TestMessages(
    std::size_t message_count, std::size_t message_size) {
  // Start test
  // Make test
  // Stop test
  StartTest().Subscribe([this, message_size, message_count]() {
    // test started
    auto payload_size = message_size > 2 ? message_size - 2 : 0;
    message_sender_ = std::make_unique<MessageSender>(
        ae_context_,
        [this, payload_size](std::uint16_t id) -> decltype(auto) {
          auto api = ApiCallAdapter{bandwidth_api_, *message_stream_};
          api->Message(id, DataBuffer(payload_size));
          return api.Flush();
        },
        message_count);

    test_res_sub_ = message_sender_->result_event().Subscribe(
        [this, message_size](auto const& result) {
          if (result) {
            StopTest().Subscribe(
                [this,
                 bandwidth = Bandwidth{result.value().duration,
                                       result.value().count, message_size}]() {
                  test_finished_event_.Emit(bandwidth);
                });
          } else {
            error_event_.Emit();
          }
        });
  });

  return test_finished_event_;
}

Event<void()> const& Sender::StartTest() {
  start_test_action_.emplace(
      ae_context_,
      [this]() {
        AE_TELED_DEBUG("Sending start test request");
        auto api = ApiCallAdapter{bandwidth_api_, *message_stream_};

        sync_subs_ += api->StartTest().Subscribe([this](auto const& res) {
          if (res.IsOk()) {
            start_test_action_->Stop();
            test_started_event_.Emit();
          }
        });
        api.Flush();
      },
      std::chrono::seconds{1}, 5);

  sync_action_failed_sub_ =
      start_test_action_->repeat_count_exceeded().Subscribe(
          [this]() { error_event_.Emit(); });

  return test_started_event_;
}

Event<void()> const& Sender::StopTest() {
  stop_test_action_.emplace(
      ae_context_,
      [this]() {
        AE_TELED_DEBUG("Sending stop test request");
        auto api = ApiCallAdapter{bandwidth_api_, *message_stream_};

        sync_subs_ += api->StopTest().Subscribe([this](auto const& res) {
          if (res.IsOk()) {
            stop_test_action_->Stop();
            test_stopped_event_.Emit();
          }
        });
        api.Flush();
      },
      std::chrono::seconds{1}, 5);

  sync_action_failed_sub_ =
      stop_test_action_->repeat_count_exceeded().Subscribe(
          [this]() { error_event_.Emit(); });

  return test_stopped_event_;
}

void Sender::OnRecvData(DataBuffer const& data) {
  auto parser = ApiParser{protocol_context_, data};
  parser.Parse(bandwidth_api_);
}
}  // namespace ae::bench
