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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_MESSAGE_SENDER_H_
#define EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_MESSAGE_SENDER_H_

#include <functional>

#include "aether/clock.h"
#include "aether/ae_context.h"
#include "aether/types/result.h"
#include "aether/events/events.h"
#include "aether/events/multi_subscription.h"
#include "aether/write_action/write_action.h"

#include "send_messages_bandwidth/common/bandwidth.h"

namespace ae::bench {
class MessageSender {
 public:
  struct SendResult {
    std::size_t count;
    HighDuration duration;
  };
  using SendProc = std::function<WriteAction&(std::uint16_t id)>;
  using ResultEvent = Event<void(Result<SendResult, int>)>;

  MessageSender(AeContext const& ae_context, SendProc send_proc,
                std::size_t send_count);

  ResultEvent::Subscriber result_event();
  void Stop();

 private:
  void Send();
  void EmitResult();

  AeContext ae_context_;
  SendProc send_proc_;
  std::size_t send_count_;
  Duration send_duration_;

  HighResTimePoint first_send_time_;
  std::uint16_t message_send_count_ = 0;
  std::size_t message_send_confirm_count_ = 0;

  MultiSubscription message_send_;
  ResultEvent result_event_;
  TaskSubscription send_sub_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_MESSAGE_SENDER_H_
