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

#ifndef AETHER_VECTOR_BUFFER_H_
#define AETHER_VECTOR_BUFFER_H_

#include <cstddef>
#include <cstring>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"

#include "aether/types/packed_size.h"

namespace ae {

template <typename SizeT>
struct VectorBuffer {
  using PackedSize = SizeT;

  seri::SeriResult Write(seri::SizeWriteTag tag) {
    auto const v = PackedSize{tag.size};
    std::uint8_t buf[PackedSize::kMaxWireBytes];
    std::size_t const n = v.Serialize(buf);
    return Write(seri::DataWriteTag{buf, n});
  }

  seri::SeriResult Write(seri::DataWriteTag tag) {
    buff.insert(std::end(buff), static_cast<std::uint8_t const*>(tag.data),
                static_cast<std::uint8_t const*>(tag.data) + tag.size);
    return Ok{seri::good};
  }

  seri::SeriResult Read(seri::SizeReadTag tag) {
    auto const available = buff.size() - read_offset;
    std::size_t const n =
        PackedSize::WireBytesNeeded(buff.data() + read_offset, available);
    if (n == 0) {
      return Error{seri::read_eof};
    }

    PackedSize decoded{};
    std::size_t const bytes_read =
        decoded.Deserialize(buff.data() + read_offset, n);
    if (bytes_read == 0) {
      return Error{seri::read_eof};
    }
    read_offset += bytes_read;
    tag.size = static_cast<std::size_t>(decoded);
    return Ok{seri::good};
  }

  seri::SeriResult Read(seri::DataReadTag tag) {
    if (tag.size > buff.size() - read_offset) {
      return Error{seri::read_eof};
    }

    std::memcpy(tag.data, buff.data() + read_offset, tag.size);
    read_offset += tag.size;

    return Ok{seri::good};
  }

  std::vector<std::uint8_t>& buff;
  std::size_t read_offset{0};
};
}  // namespace ae

#endif  // AETHER_VECTOR_BUFFER_H_
