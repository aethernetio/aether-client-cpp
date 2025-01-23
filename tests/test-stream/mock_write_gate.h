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

#ifndef TESTS_TEST_STREAM_MOCK_WRITE_GATE_H_
#define TESTS_TEST_STREAM_MOCK_WRITE_GATE_H_

#include <cstddef>
#include <utility>

#include "aether/common.h"

#include "aether/actions/action_list.h"
#include "aether/stream_api/istream.h"
#include "aether/events/events.h"

namespace ae {

class MockStreamWriteAction : public StreamWriteAction {
 public:
  explicit MockStreamWriteAction(ActionContext action_context)
      : StreamWriteAction{action_context} {
    state_.Set(State::kDone);
  }

  TimePoint Update(TimePoint current_time) override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kQueued:
        case State::kInProgress:
          break;
        case State::kDone:
          Action::Result(*this);
          break;
        case State::kStopped:
          Action::Stop(*this);
          break;
        case State::kTimeout:
        case State::kFailed:
        case State::kPanic:
          Action::Error(*this);
          break;
      }
    }
    return current_time;
  }

  void Stop() override { state_.Set(State::kStopped); }
};

class MockWriteGate : public ByteGate {
 public:
  explicit MockWriteGate(ActionContext action_context,
                         std::size_t max_data_size)
      : action_list_{action_context}, stream_info_{max_data_size, {}, {}, {}} {}

  ActionView<StreamWriteAction> Write(DataBuffer&& buffer,
                                      TimePoint current_time) override {
    on_write_.Emit(std::move(buffer), current_time);
    return action_list_.Emplace();
  }

  StreamInfo stream_info() const override { return stream_info_; }

  EventSubscriber<void(DataBuffer, TimePoint)> on_write_event() {
    return on_write_;
  }

  void WriteOut(DataBuffer const& buffer) { out_data_event_.Emit(buffer); }

 private:
  ActionList<MockStreamWriteAction> action_list_;
  StreamInfo stream_info_;
  Event<void(DataBuffer, TimePoint)> on_write_;
};
}  // namespace ae

#endif  // TESTS_TEST_STREAM_MOCK_WRITE_GATE_H_
