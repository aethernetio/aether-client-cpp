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

#include "aether/stream_api/unidirectional_gate.h"
#include "aether/stream_api/stream_write_action.h"

namespace ae {
void WriteOnlyGate::LinkOut(OutGate& out) {
  out_ = &out;

  gate_update_subscription_ = out.gate_update_event().Subscribe(
      gate_update_event_, MethodPtr<&GateUpdateEvent::Emit>{});
  gate_update_event_.Emit();
  // Do not subscribe to out_data_event
}

ReadOnlyGate::ReadOnlyGate(ActionContext action_context)
    : failed_write_actions_{action_context} {}

ActionView<StreamWriteAction> ReadOnlyGate::Write(
    DataBuffer&& /* data */, TimePoint /* current_time */) {
  return failed_write_actions_.Emplace();
}

}  // namespace ae
