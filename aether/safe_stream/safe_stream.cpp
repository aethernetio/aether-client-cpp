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

#include "aether/safe_stream/safe_stream.h"

#include <stdexcept>

namespace ae {

std::size_t SafeStreamConfig::CheckedBufferCapacity() const {
  if (window_size == 0 || window_size >= buffer_capacity / 2) {
    throw std::invalid_argument(
        "SafeStream window must be smaller than half the buffer");
  }
  if (window_size > kSafeStreamMaxWindow) {
    throw std::invalid_argument("SafeStream window exceeds the wire limit");
  }
  if (max_packet_size >= buffer_capacity) {
    throw std::invalid_argument("SafeStream packet must fit in the buffer");
  }
  return buffer_capacity;
}

SafeStream::SafeStream(AeContext const& ae_context, SafeStreamConfig config)
    : ByteStream{ae_context},
      ae_context_{ae_context},
      config_{config},
      protocol_context_(ae_context_),
      api_server_{*this},
      api_client_{},
      sender_{ae_context_, *this, config_},
      receiver_{ae_context_, *this, config_},
      stream_info_{config_.max_packet_size, config_.max_packet_size, false,
                   LinkState::kUnlinked, false} {}

SafeStream::~SafeStream() = default;

}  // namespace ae
