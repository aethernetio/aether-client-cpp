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

#include "aether/common.h"
#include "aether/actions/action.h"
#include "aether/actions/action_ptr.h"
#include "aether/types/state_machine.h"
#include "aether/actions/action_context.h"
#include "aether/events/multi_subscription.h"
#include "aether/stream_api/stream_write_action.h"

#include "send_messages_bandwidth/common/bandwidth.h"

namespace ae::bench {
class MessageSender : public Action<MessageSender> {
  enum class State : std::uint8_t {
    kSending,
    kSuccess,
    kWaitbuffer,
    kStopped,
    kError,
  };

 public:
  using SendProc =
      std::function<ActionPtr<StreamWriteAction>(std::uint16_t id)>;

  MessageSender(ActionContext action_context, SendProc send_proc,
                std::size_t send_count);

  UpdateStatus Update();

  void Stop();

  std::size_t message_send_count() const;
  Duration send_duration() const;

 private:
  void Send();

  void WaitBuffer();

  SendProc send_proc_;
  std::size_t send_count_;
  Duration send_duration_;

  HighResTimePoint first_send_time_;
  HighResTimePoint last_send_time_;
  StateMachine<State> state_;
  std::uint16_t message_send_count_ = 0;
  std::size_t message_send_confirm_count_ = 0;

  MultiSubscription message_send_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_MESSAGE_SENDER_H_
