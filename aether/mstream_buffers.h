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

#ifndef AETHER_MSTREAM_BUFFERS_H_
#define AETHER_MSTREAM_BUFFERS_H_

#include <vector>
#include <cstdint>

#include "aether/mstream.h"
#include "aether/memory_buffer.h"

namespace ae {

// implements OBuffer
template <typename SizeType = std::uint32_t>
struct VectorWriter {
  using size_type = SizeType;

  std::vector<std::uint8_t>& data_;

  VectorWriter(std::vector<std::uint8_t>& data) : data_(data) {}
  virtual ~VectorWriter() = default;

  std::size_t write(void const* data, std::size_t size) {
    data_.insert(data_.end(), reinterpret_cast<std::uint8_t const*>(data),
                 reinterpret_cast<std::uint8_t const*>(data) + size);
    return size;
  }
};

template <typename SizeType = std::uint32_t>
struct VectorReader {
  using size_type = SizeType;
  std::vector<uint8_t> const& data_;
  std::size_t offset_ = 0;
  ReadResult result_{};

  VectorReader(std::vector<uint8_t> const& data) : data_(data) {}
  virtual ~VectorReader() = default;

  size_t read(void* data, size_t size, size_t /* min_size */) {
    if (offset_ + size > data_.size()) {
      result_ = ReadResult::kNo;
      return 0;
    }
    std::memcpy(data, data_.data() + offset_, size);
    offset_ += size;
    result_ = ReadResult::kYes;
    return size;
  }

  ReadResult result() const { return result_; }
  void result(ReadResult result) { result_ = result; }
};

template <typename SizeType = std::uint32_t>
struct MemStreamReader {
  using size_type = SizeType;

  MemStreamBuf<> buffer_;

  ReadResult read_result_{};

  size_t add_data(uint8_t const* data, size_t size) {
    // expand buffer with new data
    auto s = buffer_.sputn(reinterpret_cast<char const*>(data),
                           static_cast<std::streamsize>(size));
    return static_cast<size_t>(s);
  }

  void reset_read() { buffer_.pubseekpos(0, std::ios_base::in); }
  void reset_write() { buffer_.pubseekpos(0, std::ios_base::out); }

  size_t read(void* dst, size_t size, size_t /* minimum_size */) {
    auto s = buffer_.sgetn(static_cast<char*>(dst),
                           static_cast<std::streamsize>(size));
    if (s != size) {
      read_result_ = ReadResult::kNo;
    } else {
      read_result_ = ReadResult::kYes;
    }
    return static_cast<size_t>(s);
  }

  ReadResult result() const { return read_result_; }
  void result(ReadResult result) { read_result_ = result; }
};

template <typename SizeType = std::uint32_t>
struct MemStreamWriter {
  using size_type = SizeType;

  std::size_t write(void const* data, std::size_t size) {
    buffer.xsputn(reinterpret_cast<char const*>(data),
                  static_cast<std::streamsize>(size));
    return size;
  }

  MemStreamBuf<> buffer;
};

}  // namespace ae

#endif  // AETHER_MSTREAM_BUFFERS_H_ */
