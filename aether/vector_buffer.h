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

#include <cassert>
#include <cstddef>
#include <cstring>
#include <vector>

#include <numeric/tiered_int.h>  // IWYU pragma: keep
#include "aether-miscpp/serialization/binary_archive.h"

#include "aether/tiered_int_serializer.h"

namespace ae {

template <typename TieredInt>
struct VectorBuffer {
  using PackedSize = TieredInt;

  seri::SeriResult Write(seri::SizeWriteTag tag) {
    auto v = PackedSize{tag.size};
    auto writer = seri::TIntWriter{.buffer = *this};
    v.Serialize(writer);
    return writer.res;
  }

  seri::SeriResult Write(seri::DataWriteTag tag) {
    buff.insert(std::end(buff), static_cast<std::uint8_t const*>(tag.data),
                static_cast<std::uint8_t const*>(tag.data) + tag.size);
    return Ok{seri::good};
  }

  seri::SeriResult Read(seri::SizeReadTag tag) {
    auto v = PackedSize{};
    auto reader = seri::TIntReader{.buffer = *this};
    TierDeserializeRes r = v.Deserialize(reader);
    if (r != TierDeserializeRes::kFinished) {
      return Error{seri::read_eof};
    }
    if (!reader.res) {
      return reader.res;
    }
    tag.size = static_cast<std::size_t>(v);
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
