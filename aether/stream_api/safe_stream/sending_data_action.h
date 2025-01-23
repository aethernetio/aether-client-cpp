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

#ifndef AETHER_STREAM_API_SAFE_STREAM_SENDING_DATA_ACTION_H_
#define AETHER_STREAM_API_SAFE_STREAM_SENDING_DATA_ACTION_H_

#include <utility>

#include "aether/state_machine.h"
#include "aether/events/events.h"
#include "aether/actions/action.h"
#include "aether/transport/data_buffer.h"
#include "aether/actions/action_context.h"

#include "aether/stream_api/safe_stream/safe_stream_types.h"

namespace ae {
struct SendingData {
  OffsetRange get_offset_range(SafeStreamRingIndex::type window_size) const;

  SafeStreamRingIndex offset;
  DataBuffer data;
};

class SendingDataAction : public Action<SendingDataAction> {
  enum class State : std::uint8_t {
    kWaiting,
    kSending,
    kDone,
    kStopped,
    kFailed,
  };

 public:
  using Action::Action;
  using Action::operator=;
  SendingDataAction(ActionContext action_context, SendingData data);

  TimePoint Update(TimePoint current_time) override;

  SendingData& sending_data();
  EventSubscriber<void()> stop_event();

  void Sending();
  void Stop();

  void SentConfirmed();
  void Stopped();
  void Failed();

 private:
  SendingData sending_data_;
  StateMachine<State> state_;
  Event<void()> stop_event_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SENDING_DATA_ACTION_H_
