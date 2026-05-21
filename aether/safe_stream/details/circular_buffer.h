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

#include <span>
#include <array>
#include <cstddef>
#include <algorithm>

#include "aether/types/result.h"
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
template <std::size_t Capacity>
class CircularBuffer {
 public:
  using value_type = std::uint8_t;
  static constexpr std::size_t kCapacity = Capacity;
  using index_type = RingIndex<kCapacity>;
  using index_range_type = RingIndexRange<index_type>;

  struct DSpan {
    constexpr std::size_t size() const noexcept {
      return first.size() + second.size();
    }

    std::span<value_type const> first;
    std::span<value_type const> second;
  };

  constexpr CircularBuffer() noexcept = default;
  explicit constexpr CircularBuffer(std::size_t start_offset) noexcept
      : begin_{index_type{start_offset}}, end_{begin_} {}

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
    auto available = (begin_ == pos) ? kCapacity : Distance(pos, begin_);
    if (data.size() > available) {
      return Error{CircularBufferError::kDataOverflow};
    }
    auto start = pos;
    // copy the data into array
    // expect for 2 copies depends on does data overlaps buffer or not
    while (!data.empty()) {
      auto remaining = buffer_.size() - static_cast<std::size_t>(pos);
      auto copy_size = data.size() > remaining ? remaining : data.size();
      std::copy_n(std::begin(data), copy_size,
                  buffer_.data() + static_cast<std::size_t>(pos));
      data = data.subspan(copy_size);
      pos += copy_size;
    }
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
    std::size_t first_max_size =
        buffer_.size() - static_cast<std::size_t>(start);
    std::size_t first_size = first_max_size > size ? size : first_max_size;
    res.first = std::span<value_type const>(
        buffer_.data() + static_cast<std::size_t>(start), first_size);
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
  }

  constexpr std::size_t size() const noexcept { return Distance(begin_, end_); }
  constexpr index_type begin() const noexcept { return begin_; }
  constexpr index_type end() const noexcept { return end_; }

 private:
  index_type begin_{0};
  index_type end_{0};

  std::array<value_type, kCapacity> buffer_;
};

}  // namespace ae

#endif  // AETHER_SAFE_STREAM_DETAILS_CIRCULAR_BUFFER_H_
