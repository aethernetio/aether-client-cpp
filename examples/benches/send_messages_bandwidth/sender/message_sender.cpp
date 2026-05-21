/*
 * Copyright 2025 Aethernet Inc.
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

#include "send_messages_bandwidth/sender/message_sender.h"

#include "aether/tele/tele.h"

namespace ae::bench {

MessageSender::MessageSender(AeContext const& ae_context, SendProc send_proc,
                             std::size_t send_count)
    : ae_context_{ae_context},
      send_proc_{std::move(send_proc)},
      send_count_{send_count} {
  AE_TELED_INFO("MessageSender created");
  send_sub_ = ae_context_.scheduler().Task([this]() {
    first_send_time_ = HighResTimePoint::clock::now();
    Send();
  });
}

MessageSender::ResultEvent::Subscriber MessageSender::result_event() {
  return EventSubscriber{result_event_};
}

void MessageSender::Stop() {
  AE_TELED_DEBUG("MessageSender Stop");
  send_sub_.Reset();
  result_event_.Emit(Error(0));
}

void MessageSender::Send() {
  AE_TELED_DEBUG("Sending {}", message_send_count_);

  auto& write_action = send_proc_(message_send_count_);
  message_send_ += write_action.status_event().Subscribe([&](auto status) {
    switch (status) {
      case WriteAction::Status::kSuccess: {
        message_send_confirm_count_++;
        // if all messages are sent, emit result
        if (message_send_confirm_count_ == message_send_count_) {
          EmitResult();
        }
        break;
      }
      case WriteAction::Status::kFail: {
        AE_TELED_ERROR("Error sending message");
        send_sub_.Reset();
        result_event_.Emit(Error(12));
        break;
      }
      default:
        break;
    }
  });

  ++message_send_count_;
  if (message_send_count_ != send_count_) {
    // reschedule next send
    send_sub_ = ae_context_.scheduler().Task([this]() { Send(); });
  }
}

void MessageSender::EmitResult() {
  auto last_send_time = HighResTimePoint::clock::now();

  result_event_.Emit(Ok{
      SendResult{
          .count = message_send_count_,
          .duration = last_send_time - first_send_time_,
      },
  });
}
}  // namespace ae::bench
