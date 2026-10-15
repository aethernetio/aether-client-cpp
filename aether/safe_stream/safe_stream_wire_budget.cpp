/*
 * Copyright 2026 Aethernet Inc.
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

#include "aether/safe_stream/safe_stream_wire_budget.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "aether/api_protocol/api_protocol.h"
#include "aether/safe_stream/details/safe_stream_client_api.h"

namespace ae {

DataBuffer PackSafeStreamClientSend(std::uint16_t begin_offset,
                                    std::uint16_t delta_offset,
                                    std::uint8_t repeat_count,
                                    DataBuffer data) {
  SafeStreamApiClient client;
  ApiContext context{client};
  context->Send(begin_offset, delta_offset, repeat_count, std::move(data));
  return std::move(context).Pack();
}

std::size_t PackedSafeStreamSendBytes(std::size_t send_data_bytes) {
  std::vector<std::uint8_t> bytes(send_data_bytes, 0x5A);
  return PackSafeStreamClientSend(0, 0, 0, DataBuffer{bytes}).size();
}

std::size_t MaxSafeStreamSendDataBytesForPackedSendBudget(
    std::size_t max_packed_send_bytes) {
  if (max_packed_send_bytes == 0) {
    return 0;
  }
  if (PackedSafeStreamSendBytes(0) > max_packed_send_bytes) {
    return 0;
  }
  std::size_t lo = 0;
  std::size_t hi = max_packed_send_bytes;
  while (lo < hi) {
    std::size_t mid = lo + (hi - lo + 1) / 2;
    if (PackedSafeStreamSendBytes(mid) <= max_packed_send_bytes) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

std::size_t MaxSafeStreamSendDataBytesForDownstreamMaxElementSize(
    std::size_t downstream_max_element_size) {
  return MaxSafeStreamSendDataBytesForPackedSendBudget(
      downstream_max_element_size);
}

}  // namespace ae
