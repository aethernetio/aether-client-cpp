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

#include "numeric/tiered_int.h"

#include "aether-miscpp/serialization/binary_archive.h"

namespace ae::seri {

template <BinaryBuffer B>
struct TIntWriter {
  template <typename T>
    requires(std::is_integral_v<T>)
  TIntWriter& operator<<(T const& v) {
    res = buffer.Write(seri::DataTag{v});
    return *this;
  }

  B& buffer;
  SeriResult res{Ok{seri::good}};
};

template <BinaryBuffer B>
struct TIntReader {
  template <typename T>
    requires(std::is_integral_v<T>)
  TIntReader& operator>>(T& v) {
    res = buffer.Read(seri::DataTag{v});
    return *this;
  }

  B& buffer;
  SeriResult res{Ok{seri::good}};
};

template <BinaryBuffer B, typename T1, typename T2, T2 Limit>
struct Serializer<BinaryArchive<B>, TieredInt<T1, T2, Limit>> {
  using Archive = BinaryArchive<B>;
  using TInt = TieredInt<T1, T2, Limit>;

  SeriResult Seri(Archive& archive, Meta<TInt const> meta) const {
    auto writer = TIntWriter{.buffer = archive.buffer()};
    meta.value.Serialize(writer);
    return writer.res;
  }

  SeriResult Deseri(Archive& archive, Meta<TInt> meta) const {
    auto reader = TIntReader{.buffer = archive.buffer()};
    auto r = meta.value.Deserialize(reader);
    if (r != TierDeserializeRes::kFinished) {
      return Error{read_eof};
    }
    return reader.res;
  }
};
}  // namespace ae::seri

#endif  // AETHER_TIERED_INT_SERIALIZER_H_
