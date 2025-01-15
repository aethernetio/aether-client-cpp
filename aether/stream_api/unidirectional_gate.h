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

#ifndef AETHER_STREAM_API_UNIDIRECTIONAL_GATE_H_
#define AETHER_STREAM_API_UNIDIRECTIONAL_GATE_H_

#include "aether/actions/action_list.h"
#include "aether/actions/action_context.h"
#include "aether/stream_api/istream.h"

namespace ae {
class WriteOnlyGate : public ByteGate {
 public:
  void LinkOut(OutGate& out) override;
};

class ReadOnlyGate : public ByteGate {
 public:
  explicit ReadOnlyGate(ActionContext action_context);

  ActionView<StreamWriteAction> WriteIn(DataBuffer data,
                                        TimePoint current_time) override;

 private:
  ActionList<FailedStreamWriteAction> failed_write_actions_;
};

template <typename TWriteGate, typename TReadGate>
class ParallelGate : public ByteIGate {
 public:
  ParallelGate(TWriteGate write_gate, TReadGate read_gate)
      : write_gate_{std::move(write_gate)},
        read_gate_{std::move(read_gate)},
        write_gate_update_{write_gate_.gate_update_event().Subscribe(
            [this]() { gate_update_event_.Emit(); })},
        read_gate_update_{read_gate_.gate_update_event().Subscribe(
            [this]() { gate_update_event_.Emit(); })} {}

  ActionView<StreamWriteAction> WriteIn(DataBuffer data,
                                        TimePoint current_time) override {
    return write_gate_.WriteIn(std::move(data), current_time);
  }
  typename OutDataEvent::Subscriber out_data_event() override {
    return read_gate_.out_data_event();
  }
  std::size_t max_write_in_size() const override {
    return write_gate_.max_write_in_size();
  }
  GateUpdateEvent::Subscriber gate_update_event() override {
    return gate_update_event_;
  }
  bool is_write_buffered() const override {
    return write_gate_.is_write_buffered();
  }
  std::size_t buffer_free_size() const override {
    return write_gate_.buffer_free_size();
  }
  bool is_linked() const override { return write_gate_.is_linked(); }

  auto& get_write_gate() { return write_gate_; }
  auto& get_read_gate() { return read_gate_; }

 private:
  TWriteGate write_gate_;
  TReadGate read_gate_;
  GateUpdateEvent gate_update_event_;
  Subscription write_gate_update_;
  Subscription read_gate_update_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_UNIDIRECTIONAL_GATE_H_