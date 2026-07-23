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

#include "send_messages_bandwidth/receiver/receiver.h"

#include "aether/client_messages/p2p_message_stream.h"
#include "aether/stream_api/api_call_adapter.h"

#include "aether/tele.h"

namespace ae::bench {
Receiver::Receiver(AeContext const& ae_context, Client::ptr client)
    : ae_context_{ae_context},
      client_{std::move(client)},
      bandwidth_api_{protocol_context_} {}

EventSubscriber<void()> Receiver::error_event() { return error_event_; }

void Receiver::Connect() {
  message_stream_subscription_ =
      client_->message_stream_manager().new_port_event().Subscribe(
          [this](ae::P2pPortHandle handle) {
            auto dest = handle.destination();
            AE_TELED_DEBUG("Received message stream from {}", dest);
            message_stream_ = std::make_shared<P2pStream>(
                ae_context_, client_.Load(), dest, std::move(handle));
            message_stream_->out_data_event().Subscribe(
                MethodPtr<&Receiver::OnRecvData>{this});
          });
}

void Receiver::Disconnect() { message_stream_.reset(); }

EventSubscriber<void()> Receiver::Handshake() {
  bandwidth_api_.handshake_event().Subscribe([this](RequestId req_id) {
    AE_TELED_DEBUG("Received handshake request {}", req_id);
    auto api = ApiCallAdapter{ApiContext{bandwidth_api_}, *message_stream_};
    api->return_result.SendResult(req_id, true);
    api.Flush();
    handshake_made_event_.Emit();
  });

  return handshake_made_event_;
}

EventSubscriber<void(Bandwidth const&)> Receiver::TestMessages(
    std::size_t message_count, std::size_t message_size) {
  test_start_sub_ =
      bandwidth_api_.start_test_event().Subscribe([this](RequestId req_id) {
        AE_TELED_DEBUG("Received start test request {}", req_id);
        auto api = ApiCallAdapter{ApiContext{bandwidth_api_}, *message_stream_};
        api->return_result.SendResult(req_id, true);
        api.Flush();
      });

  test_stopped_ = false;
  test_stop_sub_ =
      bandwidth_api_.stop_test_event().Subscribe([this](RequestId req_id) {
        AE_TELED_DEBUG("Received stop test request {}", req_id);
        auto api = ApiCallAdapter{ApiContext{bandwidth_api_}, *message_stream_};
        if (message_receiver_ && !test_stopped_) {
          message_receiver_->StopTest();
          test_stopped_ = true;
        }
        api->return_result.SendResult(req_id, true);
        api.Flush();
      });

  message_receiver_ = std::make_unique<MessageReceiver>(ae_context_);

  message_receiver_->result_event().Subscribe(
      [this, message_count, message_size](auto const& result) {
        if (result) {
          test_finished_event_.Emit(
              {result.value().duration, result.value().count, message_size});
        } else {
          AE_TELED_ERROR("Test messages count {}, size {}, failed",
                         message_count, message_size);
          error_event_.Emit();
        }
      });

  message_recv_sub_ =
      bandwidth_api_.message_event().Subscribe([this](auto id, auto&&) {
        test_start_sub_.Reset();
        message_receiver_->MessageReceived(id);
      });

  return test_finished_event_;
}

void Receiver::OnRecvData(DataBuffer const& data) {
  auto parser = ApiParser{protocol_context_, data};
  parser.Parse(bandwidth_api_);
}

}  // namespace ae::bench
