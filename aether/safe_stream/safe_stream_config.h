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
#include "aether/config.h"

namespace ae {
class SafeStream;
using SafeStreamOffset = std::uint32_t;
inline constexpr std::size_t kSafeStreamIndexPeriod = std::size_t{1} << 30;
// Protocol limit for a 100 MB/s stream with a 55 ms ACK horizon, rounded
// up to a whole MiB. Absolute offsets still wrap at kSafeStreamIndexPeriod.
inline constexpr std::size_t kSafeStreamMaxWindow = 6 * 1024 * 1024;

struct SafeStreamConfig {
  std::size_t window_size;        //< size of sending window
  std::size_t max_packet_size;    //< max size of sending data
  std::uint8_t max_repeat_count;  //< max repeat count for sending packet
  Duration wait_ack_timeout;      //< Timeout for waiting ack
  Duration send_ack_timeout;      //< max time to wait before send ack
  Duration send_repeat_timeout;  //< max time to wait before send repeat request
  // Wait for a writable event before submitting data to a bounded transport.
  bool wait_for_writable_transport{false};
  Duration minimum_ack_timeout{};
  // Flush a cumulative ACK early after delivering this many new bytes.
  // Zero leaves ACKs on the configured timer.
  std::size_t ack_batch_bytes{};
  // Capacity of each buffer. The window must be smaller than half of it.
  std::size_t buffer_capacity{AE_SAFE_STREAM_CAPACITY};

  std::size_t CheckedBufferCapacity() const;
};

}  // namespace ae

#endif  // AETHER_SAFE_STREAM_SAFE_STREAM_CONFIG_H_
