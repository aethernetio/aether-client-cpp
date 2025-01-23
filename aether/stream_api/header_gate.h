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

#ifndef AETHER_STREAM_API_HEADER_GATE_H_
#define AETHER_STREAM_API_HEADER_GATE_H_

#include "aether/stream_api/istream.h"

#include "aether/transport/data_buffer.h"

namespace ae {
// Add a header to data
class AddHeaderGate : public ByteGate {
 public:
  explicit AddHeaderGate(DataBuffer header);

  ActionView<StreamWriteAction> Write(DataBuffer&& buffer,
                                      TimePoint current_time) override;

  StreamInfo stream_info() const override;

 private:
  DataBuffer header_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_HEADER_GATE_H_
