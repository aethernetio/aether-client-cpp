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

#include <set>

#include "aether/common.h"
#include "aether/actions/action.h"
#include "aether/types/state_machine.h"
#include "aether/actions/action_context.h"

#include "send_messages_bandwidth/common/bandwidth.h"

namespace ae::bench {
class MessageReceiver : public Action<MessageReceiver> {
  static constexpr Duration kReceiveTimeout = std::chrono::seconds(5);

  enum class State : std::uint8_t {
    kReceiving,
    kSuccess,
    kStopped,
    kError,
  };

 public:
  explicit MessageReceiver(ActionContext action_context);
  UpdateStatus Update();

  std::size_t message_received_count() const;
  Duration receive_duration() const;

  void MessageReceived(std::uint16_t id);
  void StopTest();
  void Stop();

 private:
  UpdateStatus CheckReceiveTimeout(TimePoint current_time);

  StateMachine<State> state_;

  std::size_t message_received_count_ = 0;
  std::set<std::uint16_t> received_message_ids_;
  TimePoint receive_message_timeout_;
  HighResTimePoint first_message_received_time_;
  HighResTimePoint last_message_received_time_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_RECEIVER_MESSAGE_RECEIVER_H_
