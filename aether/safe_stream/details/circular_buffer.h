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

#ifndef AETHER_SAFE_STREAM_DETAILS_CIRCULAR_BUFFER_H_
#define AETHER_SAFE_STREAM_DETAILS_CIRCULAR_BUFFER_H_

#include <algorithm>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

#include "aether-miscpp/types/result.h"
#include "aether/safe_stream/safe_stream_config.h"
#include "aether/types/ring_index.h"

namespace ae {
enum class CircularBufferError : unsigned char {
  kDataOverflow,
  kEmptyBuffer,
  kIndexOutOfRange,
};

/**
 * \brief Circular buffer to work with sending and receiving buffers
 */
class CircularBuffer {
 public:
  using value_type = std::uint8_t;
  using index_type = RingIndex<kSafeStreamIndexPeriod>;
  using index_range_type = RingIndexRange<index_type>;

  struct DSpan {
    constexpr std::size_t size() const noexcept {
      return first.size() + second.size();
    }

    std::span<value_type const> first;
    std::span<value_type const> second;
  };

  explicit CircularBuffer(std::size_t capacity, index_type start = {})
      : begin_{start}, end_{start}, buffer_(CheckedCapacity(capacity)) {}

  /**
   * \brief Push new data and return its range
   */
  constexpr Result<index_range_type, CircularBufferError> Push(
      std::span<value_type const> data) noexcept {
    return Insert(end_, data);
  }

  /**
   * \brief Insert data at the given position
   * \param pos - the position to insert at must be after begin_
   * \param data - the data to insert
   */
  constexpr Result<index_range_type, CircularBufferError> Insert(
      index_type pos, std::span<value_type const> data) noexcept {
    auto const offset = Distance(begin_, pos);
    if (offset >= buffer_.size() || data.size() > buffer_.size() - 1 - offset) {
      return Error{CircularBufferError::kDataOverflow};
    }
    auto const start = pos;
    auto const slot = PhysicalSlot(offset);
    auto const first_size = std::min(data.size(), buffer_.size() - slot);
    std::copy_n(data.begin(), first_size, buffer_.data() + slot);
    auto const remainder = data.subspan(first_size);
    std::copy(remainder.begin(), remainder.end(), buffer_.data());
    pos += data.size();
    // also move end_ to the right
    if (IndexComparable{pos, begin_} > end_) {
      end_ = pos;
    }
    // return the range of the data that was pushed
    return Ok{index_range_type{.left = start, .right = pos - 1}};
  }

  /**
   * \brief Read the data with size from start
   * \param start - the position to read from must be in range begin_..end_
   */
  constexpr Result<DSpan, CircularBufferError> Read(
      index_type start, std::size_t size) const noexcept {
    if ((begin_ == end_) || Distance(start, end_) == 0) {
      return Error{CircularBufferError::kEmptyBuffer};
    }
    if (Distance(start, end_) > Distance(begin_, end_)) {
      return Error{CircularBufferError::kIndexOutOfRange};
    }

    DSpan res;

    // size may be bigger than the actual available data, so we clamp it
    auto max_distance = Distance(start, end_);
    size = size > max_distance ? max_distance : size;

    // handle wrapping around the buffer
    auto const buffer_offset = PhysicalSlot(Distance(begin_, start));
    auto const first_max_size = buffer_.size() - buffer_offset;
    std::size_t first_size = first_max_size > size ? size : first_max_size;
    res.first =
        std::span<value_type const>(buffer_.data() + buffer_offset, first_size);
    if (first_size < size) {
      res.second =
          std::span<value_type const>(buffer_.data(), size - first_size);
    }
    return Ok{res};
  }
  /**
   * \brief Erase data range
   * \param to - the position to erase to must be in range begin_..end_
   */
  constexpr void Erase(index_type to) noexcept {
    if (Distance(to, end_) > Distance(begin_, end_)) {
      assert(false && "Erase out of bounds");
      return;
    }
    begin_slot_ = PhysicalSlot(Distance(begin_, to));
    begin_ = to;
  }
  /**
   * \brief Erase data range from the specified position
   * \param from - the position to erase from must be in range begin_..end_
   */
  constexpr void EraseFrom(index_type from) noexcept {
    if (Distance(from, end_) > Distance(begin_, end_)) {
      assert(false && "Erase out of bounds");
      return;
    }
    end_ = from;
  }

  constexpr void Reset(std::size_t offset = 0) noexcept {
    begin_ = end_ = index_type{offset};
    begin_slot_ = 0;
  }

  constexpr std::size_t size() const noexcept { return Distance(begin_, end_); }
  constexpr index_type begin() const noexcept { return begin_; }
  constexpr index_type end() const noexcept { return end_; }

 private:
  static std::size_t CheckedCapacity(std::size_t capacity) {
    if (capacity < 3 || capacity > kSafeStreamIndexPeriod / 2) {
      throw std::invalid_argument("Invalid SafeStream buffer capacity");
    }
    return capacity;
  }

  // Anchor physical storage to the first live byte. The wire index can wrap
  // independently of buffer capacity, including for capacities such as 20 KiB.
  constexpr std::size_t PhysicalSlot(std::size_t offset) const noexcept {
    auto const slot = begin_slot_ + offset;
    return slot < buffer_.size() ? slot : slot - buffer_.size();
  }

  index_type begin_;
  index_type end_;
  std::size_t begin_slot_{};
  std::vector<value_type> buffer_;
};

}  // namespace ae

#endif  // AETHER_SAFE_STREAM_DETAILS_CIRCULAR_BUFFER_H_
