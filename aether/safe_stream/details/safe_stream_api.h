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

#include "aether/types/data_buffer.h"
#include "aether/api_protocol/api_protocol.h"

namespace ae {
class SafeStreamApi : public ApiClassImpl<SafeStreamApi> {
 public:
  explicit SafeStreamApi(ProtocolContext& protocol_context)
      : ApiClassImpl{protocol_context},
        ack{protocol_context},
        request_repeat{protocol_context},
        send_reset{protocol_context},
        send{protocol_context} {}

  virtual ~SafeStreamApi() = default;

  Method<3, void(std::uint16_t index)> ack;
  Method<4, void(std::uint16_t index)> request_repeat;
  Method<5, void(std::uint16_t index, std::uint16_t delta_offset,
                 std::uint8_t repeat_count, DataBuffer data)>
      send_reset;
  Method<6, void(std::uint16_t index, std::uint16_t delta_offset,
                 std::uint8_t repeat_count, DataBuffer data)>
      send;

  /**
   * \brief Acknowledgment for data buffer up to index
   */
  virtual void AckImpl(std::uint16_t index) = 0;
  /**
   * \brief Request receive data from the index
   */
  virtual void RequestRepeatImpl(std::uint16_t index) = 0;
  virtual void SendResetImpl(std::uint16_t begin_offset,
                             std::uint16_t delta_offset,
                             std::uint8_t repeat_count, DataBuffer data) = 0;
  virtual void SendImpl(std::uint16_t begin_offset, std::uint16_t delta_offset,
                        std::uint8_t repeat_count, DataBuffer data) = 0;

  AE_METHODS(RegMethod<3, &SafeStreamApi::AckImpl>,
             RegMethod<4, &SafeStreamApi::RequestRepeatImpl>,
             RegMethod<5, &SafeStreamApi::SendResetImpl>,
             RegMethod<6, &SafeStreamApi::SendImpl>);
};

}  // namespace ae

#endif  // AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_API_H_
