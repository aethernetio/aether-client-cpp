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

#ifndef AETHER_SAFE_STREAM_SAFE_STREAM_CONFIG_H_
#define AETHER_SAFE_STREAM_SAFE_STREAM_CONFIG_H_

#include <cstdint>

#include "aether/clock.h"

namespace ae {
template <std::size_t Capacity, typename Offset = std::uint16_t>
class SafeStream;

struct SafeStreamConfig {
  std::size_t window_size;        //< size of sending window
  std::size_t max_packet_size;    //< max size of sending data
  std::uint8_t max_repeat_count;  //< max repeat count for sending packet
  Duration wait_ack_timeout;      //< Timeout for waiting ack
  Duration send_ack_timeout;      //< max time to wait before send ack
  Duration send_repeat_timeout;  //< max time to wait before send repeat request
  // Wait for a writable event before submitting data to a bounded transport.
  bool wait_for_writable_transport{false};
  // Opt into 32-bit offsets in P2pSafeStream. Both peers must use the same mode.
  bool wide_offsets{false};
  Duration minimum_ack_timeout{};
};

}  // namespace ae

#endif  // AETHER_SAFE_STREAM_SAFE_STREAM_CONFIG_H_
