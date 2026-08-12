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

#ifndef AETHER_API_PROTOCOL_API_MESSAGE_H_
#define AETHER_API_PROTOCOL_API_MESSAGE_H_

#include <cassert>
#include <cstdint>
#include <tuple>
#include <vector>

#include <numeric/tiered_int.h>

#include "aether-miscpp/serialization/serialization.h"

#include "aether/vector_buffer.h"

namespace ae {

using MessageId = std::uint8_t;

using PackedSize = TieredInt<std::uint64_t, std::uint8_t, 250>;

class ApiParser;
class ApiPacker;

using MessageBuffer = VectorBuffer<PackedSize>;

/**
 * \brief A message formed from template parameters
 */
template <typename... Ts>
struct GenericMessage {
  explicit GenericMessage() = default;
  explicit GenericMessage(Ts... args) : fields{std::forward<Ts>(args)...} {}

  [[no_unique_address]] std::tuple<Ts...> fields;
};

template <>
struct GenericMessage<> {
  explicit GenericMessage() = default;
};

namespace seri {
template <Archive A, typename... Ts>
struct Serializer<A, GenericMessage<Ts...>> {
  SeriResult Seri(A& archive, Meta<GenericMessage<Ts...> const> meta) const {
    if constexpr (sizeof...(Ts) > 0) {
      return std::apply(
          [&](auto&... args) {
            auto res = SeriResult{Ok{good}};
            auto b = ((res = archive.Save(Meta{.value = args, .name = "field"}),
                       !!res) &&
                      ...);
            (void)b;
            return res;
          },
          meta.value.fields);
    }
    return Ok{seri::good};
  }

  SeriResult Deseri(A& archive, Meta<GenericMessage<Ts...>> meta) const {
    if constexpr (sizeof...(Ts) > 0) {
      return std::apply(
          [&](auto&... args) {
            auto res = SeriResult{Ok{good}};
            auto b = ((res = archive.Load(Meta{args}), !!res) && ...);
            (void)b;
            return res;
          },
          meta.value.fields);
    }
    return Ok{seri::good};
  }
};
}  // namespace seri

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_API_MESSAGE_H_
