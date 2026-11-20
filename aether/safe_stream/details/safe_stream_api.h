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

#ifndef AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_API_H_
#define AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_API_H_

#include <cstdint>

#include "aether/api_protocol/api_protocol.h"
#include "aether/safe_stream/safe_stream_config.h"
#include "aether/types/data_buffer.h"
#include "aether/types/packed_size.h"

namespace ae {
// Reserve 96 two-byte values to cover 5,296 * 1,188 bytes in four bytes.
// With 40 extension codes in the first byte, the two-byte tier still covers
// the reference 10 KiB window. That leaves 216 one-byte values.
using SafeStreamWireDelta = TieredInt<std::uint8_t, 215, 10359>;
static_assert(SafeStreamWireDelta::kWireTier0 + 1 >= 200);
static_assert(SafeStreamWireDelta::kMaxWireBytes == sizeof(SafeStreamOffset));
static_assert(SafeStreamWireDelta::kUpper >= 5296 * 1188);
static_assert(SafeStreamWireDelta::kUpper >= kSafeStreamMaxWindow - 1);

class SafeStreamApi : public DeclareApi<SafeStreamApi> {
  using SelfType = SafeStreamApi;
  static constexpr unsigned kFirstMethod = 7;

 public:
  virtual ~SafeStreamApi() = default;

  virtual void Ack(SafeStreamOffset index) = 0;
  virtual void RequestRepeat(SafeStreamOffset index) = 0;
  virtual void SendReset(SafeStreamOffset index,
                         SafeStreamWireDelta delta_offset,
                         std::uint8_t repeat_count, DataBuffer data) = 0;
  virtual void Send(SafeStreamOffset index, SafeStreamWireDelta delta_offset,
                    std::uint8_t repeat_count, DataBuffer data) = 0;

  API_LIST(METHOD(kFirstMethod, Ack), METHOD(kFirstMethod + 1, RequestRepeat),
           METHOD(kFirstMethod + 2, SendReset), METHOD(kFirstMethod + 3, Send))
};

}  // namespace ae

#endif  // AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_API_H_
