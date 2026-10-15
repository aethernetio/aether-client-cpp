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

#ifndef AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_CLIENT_API_H_
#define AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_CLIENT_API_H_

#include "aether/safe_stream/details/safe_stream_api.h"

namespace ae {
class SafeStreamApiClient : public ISafeStreamApi {
 public:
  void Ack(std::uint16_t index) override {
    ClientMethod<&ISafeStreamApi::Ack>(index);
  }
  void RequestRepeat(std::uint16_t index) override {
    ClientMethod<&ISafeStreamApi::RequestRepeat>(index);
  }
  void SendReset(std::uint16_t begin_offset, std::uint16_t delta_offset,
                 std::uint8_t repeat_count, DataBuffer data) override {
    ClientMethod<&ISafeStreamApi::SendReset>(begin_offset, delta_offset,
                                             repeat_count, data);
  }
  void Send(std::uint16_t begin_offset, std::uint16_t delta_offset,
            std::uint8_t repeat_count, DataBuffer data) override {
    ClientMethod<&ISafeStreamApi::Send>(begin_offset, delta_offset,
                                        repeat_count, data);
  }
};
}  // namespace ae

#endif  // AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_CLIENT_API_H_
