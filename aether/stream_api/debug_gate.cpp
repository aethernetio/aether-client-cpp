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

DataBuffer DebugGate::WriteIn(DataBuffer buffer) {
  AE_TELED_DEBUG(in_format_.c_str(), buffer);
  return buffer;
}

DataBuffer DebugGate::WriteOut(DataBuffer buffer) {
  AE_TELED_DEBUG(out_format_.c_str(), buffer);
  return buffer;
}
}  // namespace ae
