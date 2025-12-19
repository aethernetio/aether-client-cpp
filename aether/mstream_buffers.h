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
#include <cassert>
#include <cstring>

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

  VectorReader(std::vector<uint8_t> const& d) : data{d} {}
  virtual ~VectorReader() = default;

  size_t read(void* d, size_t size) {
    assert(((offset + size) <= data.size()) && "Read overflow");
    std::memcpy(d, data.data() + offset, size);
    offset += size;
    return size;
  }

  std::vector<uint8_t> const& data;
  std::size_t offset = 0;
};

/**
 * \brief VectorWriter with limit in size
 */
template <typename SizeType = std::uint32_t>
struct LimitVectorWriter {
  using size_type = SizeType;

  LimitVectorWriter(std::vector<std::uint8_t>& buffer, std::size_t l)
      : data_buffer{buffer}, limit{l} {}

  std::size_t write(void const* data, std::size_t size) {
    if (end || ((data_buffer.size() + size) >= limit)) {
      // end of writing
      end = true;
      return 0;
    }
    data_buffer.insert(data_buffer.end(),
                       reinterpret_cast<std::uint8_t const*>(data),
                       reinterpret_cast<std::uint8_t const*>(data) + size);
    return size;
  }

  std::vector<std::uint8_t>& data_buffer;
  std::size_t limit;
  bool end = false;
};

template <typename SizeType = std::uint32_t>
struct MemStreamReader {
  using size_type = SizeType;

  size_t add_data(uint8_t const* data, size_t size) {
    // expand buffer with new data
    auto s = buffer.sputn(reinterpret_cast<char const*>(data),
                          static_cast<std::streamsize>(size));
    return static_cast<size_t>(s);
  }

  void reset_read() { buffer.pubseekpos(0, std::ios_base::in); }
  void reset_write() { buffer.pubseekpos(0, std::ios_base::out); }

  size_t read(void* dst, size_t size) {
    auto s = buffer.sgetn(static_cast<char*>(dst),
                          static_cast<std::streamsize>(size));
    assert((s == size) && "Read overflow");
    return static_cast<size_t>(s);
  }

  MemStreamBuf<> buffer;
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
