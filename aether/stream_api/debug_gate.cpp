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

#include "aether/stream_api/debug_gate.h"

#include <utility>

#include "aether/tele/tele.h"

namespace ae {
DebugGate::DebugGate(std::string in_format, std::string out_format)
    : in_format_{std::move(in_format)}, out_format_{std::move(out_format)} {}

ActionView<StreamWriteAction> DebugGate::Write(ByteGate::TypeIn&& in_data,
                                               TimePoint current_time) {
  assert(out_);
  AE_TELED_DEBUG(in_format_.c_str(), in_data);
  return out_->Write(std::move(in_data), current_time);
}

void DebugGate::LinkOut(OutGate& out) {
  out_ = &out;

  out_data_subscription_ = out.out_data_event().Subscribe(
      *this, MethodPtr<&DebugGate::OnDataEvent>{});

  gate_update_subscription_ = out.gate_update_event().Subscribe(
      gate_update_event_, MethodPtr<&GateUpdateEvent::Emit>{});

  gate_update_event_.Emit();
}

void DebugGate::OnDataEvent(DataBuffer const& out_data) {
  AE_TELED_DEBUG(out_format_.c_str(), out_data);
  out_data_event_.Emit(out_data);
}

}  // namespace ae
