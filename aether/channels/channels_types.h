/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_CHANNELS_CHANNELS_TYPES_H_
#define AETHER_CHANNELS_CHANNELS_TYPES_H_

#include <cstddef>
#include <cstdint>

#include "aether/reflect/reflect.h"

namespace ae {
enum class ConnectionType : std::uint8_t {
  kConnectionFull,  // Transport requires connection to be established
  kConnectionLess,  // Transport does not require connection to be established
};

enum class Reliability : std::uint8_t {
  kReliable,    // Transport is fully reliable
  kUnreliable,  // Transport is unreliable
};

struct ChannelTransportProperties {
  AE_REFLECT_MEMBERS(max_packet_size, rec_packet_size, connection_type,
                     reliability)

  std::uint32_t max_packet_size;  // < Maximum packet size in bytes
  std::uint32_t rec_packet_size;  // < Recommended packet size in bytes
  ConnectionType connection_type;
  Reliability reliability;
};
}  // namespace ae

#endif  // AETHER_CHANNELS_CHANNELS_TYPES_H_
