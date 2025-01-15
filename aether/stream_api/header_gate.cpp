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

#include "aether/stream_api/header_gate.h"

#include <utility>

#include "aether/mstream.h"
#include "aether/mstream_buffers.h"

namespace ae {
AddHeaderGate::AddHeaderGate(DataBuffer header) : header_(std::move(header)) {}

ActionView<StreamWriteAction> AddHeaderGate::WriteIn(DataBuffer buffer,
                                                     TimePoint current_time) {
  assert(out_);

  auto data_packet = DataBuffer{};
  data_packet.reserve(header_.size() + buffer.size());
  VectorWriter vw{data_packet};
  auto os = omstream{vw};
  os.write(header_.data(), header_.size());
  os.write(buffer.data(), buffer.size());

  return out_->WriteIn(std::move(data_packet), current_time);
}

std::size_t AddHeaderGate::max_write_in_size() const {
  assert(out_);
  return out_->max_write_in_size() - header_.size();
}

}  // namespace ae