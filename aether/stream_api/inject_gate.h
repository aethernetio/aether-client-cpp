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

#ifndef AETHER_STREAM_API_INJECT_GATE_H_
#define AETHER_STREAM_API_INJECT_GATE_H_

#include "aether/stream_api/istream.h"

namespace ae {
class InjectGate final : public ByteGate {
 public:
  void OutData(DataBuffer const& data) { out_data_event_.Emit(data); }
};
}  // namespace ae

#endif  // AETHER_STREAM_API_INJECT_GATE_H_
