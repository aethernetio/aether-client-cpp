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

#ifndef TESTS_TEST_STREAM_MOCK_BAD_STREAMS_H_
#define TESTS_TEST_STREAM_MOCK_BAD_STREAMS_H_

#include <optional>

#include "aether/common.h"
#include "aether/stream_api/istream.h"
#include "aether/actions/action_list.h"
#include "aether/actions/action_context.h"

namespace ae {
namespace bad_streams_internal {
class DoneStreamWriteAction : public StreamWriteAction {
 public:
  explicit DoneStreamWriteAction(ActionContext action_context);
  using StreamWriteAction::StreamWriteAction;
};
}  // namespace bad_streams_internal

class LostPacketsStream : public ByteStream {
 public:
  LostPacketsStream(ActionContext action_context, float loss_rate);

  ActionView<StreamWriteAction> Write(DataBuffer&& data_buffer) override;

  void LinkOut(ByteIStream& out) override;

 private:
  float loss_rate_;
  ActionList<bad_streams_internal::DoneStreamWriteAction> lost_packet_actions_;
};

class PacketDelayStream : public ByteStream {
  class PacketDelayAction : public Action<PacketDelayAction> {
   public:
    PacketDelayAction(ActionContext action_context, ByteIStream& out,
                      DataBuffer&& data_buffer, Duration duration);

    using Action::Action;

    ActionResult Update(TimePoint current_time);

   private:
    ByteIStream* out_;
    DataBuffer data_buffer_;
    Duration timeout_;
    std::optional<TimePoint> send_time_;
  };

 public:
  PacketDelayStream(ActionContext action_context, float delay_rate,
                    Duration max_delay);

  ActionView<StreamWriteAction> Write(DataBuffer&& data_buffer) override;

  void LinkOut(ByteIStream& out) override;

 private:
  float delay_rate_;
  Duration max_delay_;
  ActionList<bad_streams_internal::DoneStreamWriteAction>
      reordered_packet_actions_;
  ActionList<PacketDelayAction> packet_delay_actions_;
};
}  // namespace ae

#endif  // TESTS_TEST_STREAM_MOCK_BAD_STREAMS_H_
