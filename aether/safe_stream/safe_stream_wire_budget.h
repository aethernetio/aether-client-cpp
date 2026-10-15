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

#ifndef AETHER_SAFE_STREAM_SAFE_STREAM_WIRE_BUDGET_H_
#define AETHER_SAFE_STREAM_SAFE_STREAM_WIRE_BUDGET_H_

#include <cstddef>

#include "aether/types/data_buffer.h"

namespace ae {

DataBuffer PackSafeStreamClientSend(std::uint16_t begin_offset,
                                    std::uint16_t delta_offset,
                                    std::uint8_t repeat_count,
                                    DataBuffer data);

std::size_t PackedSafeStreamSendBytes(std::size_t send_data_bytes);

// Max bytes in ISafeStreamApi::Send data for a packed Send() <= max_packed_bytes.
std::size_t MaxSafeStreamSendDataBytesForPackedSendBudget(
    std::size_t max_packed_send_bytes);

// Downstream ByteIStream max_element_size is max packed SafeStream client Send().
std::size_t MaxSafeStreamSendDataBytesForDownstreamMaxElementSize(
    std::size_t downstream_max_element_size);

}  // namespace ae

#endif  // AETHER_SAFE_STREAM_SAFE_STREAM_WIRE_BUDGET_H_
