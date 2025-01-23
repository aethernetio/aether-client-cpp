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

#ifndef AETHER_STREAM_API_BUFFER_GATE_H_
#define AETHER_STREAM_API_BUFFER_GATE_H_

#include <list>
#include <cstddef>

#include "aether/common.h"

#include "aether/actions/action_list.h"
#include "aether/actions/action_context.h"
#include "aether/events/multi_subscription.h"

#include "aether/stream_api/istream.h"

namespace ae {
// TODO: add Buffer gate tests
/**
 * \brief Buffers write requests until gate is not ready to accept them.
 */
class BufferGate final : public ByteGate {
  class BufferedWriteAction final : public StreamWriteAction {
   public:
    BufferedWriteAction(ActionContext action_context, DataBuffer data,
                        TimePoint current_time);

    AE_CLASS_MOVE_ONLY(BufferedWriteAction)

    TimePoint Update(TimePoint current_time) override;
    void Stop() override;
    void Send(ByteIGate& out_gate);
    bool is_sent() const;
    std::size_t size() const;

   private:
    DataBuffer data_;
    std::size_t data_size_;
    TimePoint current_time_;
    bool is_sent_{false};

    ActionView<StreamWriteAction> write_action_;

    Subscription state_changed_subscription_;
    MultiSubscription write_action_subscription_;
  };

 public:
  explicit BufferGate(ActionContext action_context,
                      std::size_t buffer_max = static_cast<std::size_t>(100));

  AE_CLASS_NO_COPY_MOVE(BufferGate)

  ActionView<StreamWriteAction> Write(DataBuffer&& data,
                                      TimePoint current_time) override;

  void LinkOut(OutGate& out) override;

  StreamInfo stream_info() const override;

 private:
  void SetSoftWriteable(bool value);
  void UpdateGate();
  void DrainBuffer(OutGate& out);

  ActionContext action_context_;
  std::size_t buffer_max_;

  StreamInfo stream_info_;
  StreamInfo last_out_stream_info_;
  ActionList<FailedStreamWriteAction> failed_write_list_;
  std::list<BufferedWriteAction> write_in_buffer_;
  MultiSubscription write_in_subscription_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_BUFFER_GATE_H_
