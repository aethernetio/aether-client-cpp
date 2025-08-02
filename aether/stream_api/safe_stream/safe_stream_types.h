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

#ifndef AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_TYPES_H_
#define AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_TYPES_H_

#include <cstdint>

#include "aether/common.h"
#include "aether/types/ring_buffer.h"
#include "aether/types/data_buffer.h"

namespace ae {

struct SafeStreamInit {
  std::uint16_t offset;
  std::uint16_t window_size;
  std::uint16_t max_packet_size;

  AE_REFLECT_MEMBERS(offset, window_size, max_packet_size)
};

using SSRingIndex = RingIndex<std::uint32_t>;

struct OffsetRange {
  SSRingIndex left;
  SSRingIndex right;

  // offset in [begin:end] range
  constexpr bool InRange(SSRingIndex offset) const {
    return ((left == offset) || left.IsBefore(offset)) &&
           ((right == offset) || right.IsAfter(offset));
  }

  // offset range is before offset ( end < offset)
  constexpr bool IsBefore(SSRingIndex offset) const {
    return right.IsBefore(offset);
  }

  // offset range is after offset ( begin > offset)
  constexpr bool IsAfter(SSRingIndex offset) const {
    return left.IsAfter(offset);
  }

  constexpr auto distance() const { return left.Distance(right); }

  constexpr bool operator==(const OffsetRange& other) const {
    return (left == other.left) && (right == other.right);
  }

  constexpr bool operator!=(const OffsetRange& other) const {
    return !(*this == other);
  }
};

/**
 * \brief The data message used for sending data by Safe Stream Api.
 */
struct DataMessage {
  DataMessage() = default;
  DataMessage(std::uint8_t repeat_count, bool reset, std::uint16_t delta,
              DataBuffer&& d)
      : control{static_cast<std::uint8_t>(repeat_count & 0x1F),
                (reset ? std::uint8_t{0x01} : std::uint8_t{0x00}),
                {}},
        delta_offset{delta},
        data{std::move(d)} {}

  AE_CLASS_COPY_MOVE(DataMessage)

  AE_REFLECT(ae::reflect::reflect_internal::FieldGetter<DataMessage, Control>{},
             AE_MMBRS(delta_offset, data))

  std::uint8_t repeat_count() const {
    return static_cast<std::uint8_t>(control.repeat_count);
  }
  bool reset() const { return control.reset != 0; }

  struct Control {
    static std::uint8_t* get(DataMessage* obj) {
      return reinterpret_cast<std::uint8_t*>(&obj->control);
    }

    std::uint8_t repeat_count : 5;
    std::uint8_t reset : 1;
    std::uint8_t reserved : 2;
  } control;                   // control flags related to message
  std::uint16_t delta_offset;  // data offset form sender's buffer begin
  DataBuffer data;
};

/**
 * \brief Sending data stored in sending data buffer
 */
struct SendingData {
  SendingData(SSRingIndex off, DataBuffer&& d)
      : offset(off),
        data(std::move(d)),
        begin{std::begin(data)},
        end{std::end(data)} {}

  auto offset_range() const {
    auto distance = end - begin;
    return OffsetRange{offset,
                       offset + static_cast<SSRingIndex::type>(distance - 1)};
  }

  std::size_t size() const { return static_cast<std::size_t>(end - begin); }

  SSRingIndex offset;
  DataBuffer data;
  DataBuffer::iterator begin;
  DataBuffer::iterator end;
};

/**
 * \brief Saved information about sending chunk
 */
struct SendingChunk {
  OffsetRange offset_range;
  std::uint8_t repeat_count;
  TimePoint send_time;
};

/**
 * \brief The data chunk retrieved from sending data buffer to send
 */
struct DataChunk {
  DataBuffer data;
  SSRingIndex offset;
};

/**
 * \brief Received chunk of data stored in receiving chunk list
 */
struct ReceivingChunk {
  ReceivingChunk(SSRingIndex off, DataBuffer&& d, std::uint8_t rc)
      : data{std::move(d)},
        repeat_count{rc},
        offset{off},
        begin{std::begin(data)},
        end{std::end(data)} {}

  auto offset_range() const {
    auto distance = end - begin;
    return OffsetRange{offset,
                       offset + static_cast<SSRingIndex::type>(distance - 1)};
  }

  DataBuffer data;
  std::uint8_t repeat_count;
  SSRingIndex offset;
  DataBuffer::iterator begin;
  DataBuffer::iterator end;
};

/**
 * \brief Data chunk missed in receiving chunk list
 */
struct MissedChunk {
  SSRingIndex expected_offset;
  ReceivingChunk* chunk;
};

struct ChunkAcknowledge {
  OffsetRange offset_range;
  std::uint8_t repeat_count;
  bool acknowledged;
};

}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_TYPES_H_
