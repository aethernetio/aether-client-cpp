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

#ifndef AETHER_TIERED_INT_SERIALIZER_H_
#define AETHER_TIERED_INT_SERIALIZER_H_

#include <cstddef>
#include <cstdint>

#include "ae-numeric/tiered_int.h"
#include "ae-numeric/wire_io.h"

#include "aether-miscpp/serialization/binary_archive.h"

namespace ae::seri {

// BinaryArchive adapter for ae-numeric TieredInt via wire_traits /
// Serialize / Deserialize. Truncated input maps to read_eof (no exceptions,
// no heap allocations). The same wire_io entry points can later back
// FixedPoint / Exponential serializers without changing BinaryArchive.
template <BinaryBuffer B, typename WireCell, std::uint32_t... TierMaxVals>
struct Serializer<BinaryArchive<B>, TieredInt<WireCell, TierMaxVals...>> {
  using Archive = BinaryArchive<B>;
  using TInt = TieredInt<WireCell, TierMaxVals...>;

  SeriResult Seri(Archive& archive, Meta<TInt const> meta) const {
    std::uint8_t buf[MaxWireBytes<TInt>()];
    std::size_t const n = ae::Serialize(meta.value, buf);
    return archive.buffer().Write(DataWriteTag{buf, n});
  }

  SeriResult Deseri(Archive& archive, Meta<TInt> meta) const {
    std::uint8_t buf[MaxWireBytes<TInt>()]{};
    std::size_t len = 0;

    // Grow one byte at a time until WireBytesNeeded reports a complete value.
    // Avoids reading past this field into the next wire value.
    while (len < MaxWireBytes<TInt>()) {
      auto const read_res =
          archive.buffer().Read(DataReadTag{buf + len, std::size_t{1}});
      if (!read_res) {
        return read_res;
      }
      ++len;

      if (TInt::WireBytesNeeded(buf, len) == 0) {
        continue;
      }

      auto const decoded = ae::Deserialize<TInt>(buf, len);
      if (decoded.bytes_read == 0 || decoded.bytes_read != len) {
        return Error{read_eof};
      }
      meta.value = decoded.value;
      return Ok{good};
    }
    return Error{read_eof};
  }
};

}  // namespace ae::seri

#endif  // AETHER_TIERED_INT_SERIALIZER_H_
