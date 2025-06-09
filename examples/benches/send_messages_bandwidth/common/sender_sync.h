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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_SENDER_SYNC_H_
#define EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_SENDER_SYNC_H_

#include <cstddef>
#include <cstdint>

#include "aether/common.h"
#include "aether/ptr/ptr.h"
#include "aether/state_machine.h"
#include "aether/actions/action.h"
#include "aether/stream_api/istream.h"
#include "aether/api_protocol/protocol_context.h"

namespace ae::bench {
class SenderSyncAction : public Action<SenderSyncAction> {
  static constexpr Duration kSendInterval = std::chrono::seconds{1};
  static constexpr std::size_t kRepeatCount = 20;

  enum class State : std::uint8_t {
    kSendSync,
    kWaitResponse,
    kSuccess,
    kError,
  };

 public:
  explicit SenderSyncAction(ActionContext action_context,
                            ProtocolContext& protocol_context,
                            ByteIStream& stream);

  ActionResult Update();

 private:
  void SendSync(TimePoint current_time);
  TimePoint CheckInterval(TimePoint current_time);

  ProtocolContext& protocol_context_;
  ByteIStream* stream_;

  StateMachine<State> state_;
  std::size_t current_repeat_;
  TimePoint send_time_;

  Subscription state_changed_subscription_;
};

}  // namespace ae::bench
#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_SENDER_SYNC_H_
