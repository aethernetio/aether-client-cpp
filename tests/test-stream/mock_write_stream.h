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

#ifndef TESTS_TEST_STREAM_MOCK_WRITE_STREAM_H_
#define TESTS_TEST_STREAM_MOCK_WRITE_STREAM_H_

#include <cstddef>
#include <utility>

#include "aether/events/events.h"
#include "aether/stream_api/istream.h"
#include "aether/actions/action_ptr.h"

namespace ae {
class MockStreamWriteAction : public StreamWriteAction {
 public:
  explicit MockStreamWriteAction(ActionContext action_context)
      : StreamWriteAction{action_context} {
    state_.Set(State::kDone);
  }

  void Stop() override { state_.Set(State::kStopped); }
};

class MockWriteStream : public ByteStream {
 public:
  explicit MockWriteStream(ActionContext action_context,
                           std::size_t max_data_size)
      : action_context_{action_context},
        stream_info_{max_data_size, max_data_size, {}, {}, {}} {}

  ActionPtr<StreamWriteAction> Write(DataBuffer&& buffer) override {
    on_write_.Emit(std::move(buffer));
    return ActionPtr<MockStreamWriteAction>{action_context_};
  }

  StreamInfo stream_info() const override { return stream_info_; }
  StreamUpdateEvent::Subscriber stream_update_event() override {
    return EventSubscriber{stream_update_event_};
  }
  OutDataEvent::Subscriber out_data_event() override {
    return EventSubscriber{out_data_event_};
  }

  EventSubscriber<void(DataBuffer&&)> on_write_event() { return on_write_; }

  void WriteOut(DataBuffer const& buffer) { out_data_event_.Emit(buffer); }

 private:
  ActionContext action_context_;
  StreamInfo stream_info_;
  Event<void(DataBuffer&&)> on_write_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
};
}  // namespace ae

#endif  // TESTS_TEST_STREAM_MOCK_WRITE_STREAM_H_
