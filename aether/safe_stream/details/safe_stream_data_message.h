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

#ifndef AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_DATA_MESSAGE_H_
#define AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_DATA_MESSAGE_H_

#include <cstdint>

#include "aether/types/data_buffer.h"

namespace ae {
/**
 * \brief The data message used for sending data by Safe Stream Api.
 */
struct DataMessage {
  bool reset;
  std::uint8_t repeat_count;
  std::uint16_t delta_offset;  // data offset form sender's buffer begin
  DataBuffer data;
};
}  // namespace ae

#endif  // AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_DATA_MESSAGE_H_
