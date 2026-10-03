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
#include "aether/types/data_buffer.h"

namespace ae {
class ISafeStreamApi : public DeclareApi<ISafeStreamApi> {
 public:
  virtual ~ISafeStreamApi() = default;

  virtual void Ack(std::uint16_t index) = 0;
  virtual void RequestRepeat(std::uint16_t index) = 0;
  virtual void SendReset(std::uint16_t index, std::uint16_t delta_offset,
                         std::uint8_t repeat_count, DataBuffer data) = 0;
  virtual void Send(std::uint16_t index, std::uint16_t delta_offset,
                    std::uint8_t repeat_count, DataBuffer data) = 0;

  API_LIST(METHOD(3, Ack), METHOD(4, RequestRepeat), METHOD(5, SendReset),
           METHOD(6, Send))
};

}  // namespace ae

#endif  // AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_API_H_
