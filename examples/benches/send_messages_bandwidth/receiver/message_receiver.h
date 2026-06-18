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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_RECEIVER_MESSAGE_RECEIVER_H_
#define EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_RECEIVER_MESSAGE_RECEIVER_H_

#include "aether/clock.h"
#include "aether/ae_context.h"
#include "aether-miscpp/types/result.h"
#include "aether/events/events.h"

#include "send_messages_bandwidth/common/bandwidth.h"

namespace ae::bench {
class MessageReceiver {
  static constexpr Duration kReceiveTimeout = std::chrono::seconds(5);

 public:
  struct RecvResult {
    std::size_t count;
    HighDuration duration;
  };

  using ResultEvent = Event<void(Result<RecvResult, int>)>;

  explicit MessageReceiver(AeContext const& ae_context);

  ResultEvent::Subscriber result_event();

  void MessageReceived(std::uint16_t id);
  void StopTest();
  void Stop();

 private:
  void SchedulerReceiveTimeout();
  void EmitResult();

  AeContext ae_context_;

  std::size_t message_received_count_ = 0;
  TimePoint received_message_time_;
  HighResTimePoint first_message_received_time_;
  HighResTimePoint last_message_received_time_;
  ResultEvent result_event_;
  TaskSubscription wait_sub_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_RECEIVER_MESSAGE_RECEIVER_H_
