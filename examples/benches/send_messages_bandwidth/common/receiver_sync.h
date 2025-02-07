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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_RECEIVER_SYNC_H_
#define EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_RECEIVER_SYNC_H_

#include <cstddef>
#include <cstdint>

#include "aether/ptr/ptr.h"
#include "aether/state_machine.h"
#include "aether/actions/action.h"
#include "aether/stream_api/istream.h"
#include "aether/events/event_subscription.h"
#include "aether/api_protocol/send_result.h"
#include "aether/api_protocol/protocol_context.h"

namespace ae::bench {
class ReceiverSyncAction : public Action<ReceiverSyncAction> {
  static constexpr Duration kTimeout = std::chrono::seconds{5};

  enum class State : std::uint8_t {
    kWait,
    kReceived,
    kError,
  };

 public:
  ReceiverSyncAction(ActionContext action_context,
                     ProtocolContext& protocol_context, Ptr<ByteStream> stream);

  TimePoint Update(TimePoint current_time) override;

 private:
  void OnReceivedSync(RequestId request_id);
  TimePoint CheckTimeout(TimePoint current_time);

  ProtocolContext& protocol_context_;
  Ptr<ByteStream> stream_;

  StateMachine<State> state_;
  TimePoint start_time_;
  Subscription state_changed_subscription_;
  Subscription received_sync_subscription_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_RECEIVER_SYNC_H_
