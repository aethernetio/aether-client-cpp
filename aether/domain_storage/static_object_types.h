/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_DOMAIN_STORAGE_STATIC_OBJECT_TYPES_H_
#define AETHER_DOMAIN_STORAGE_STATIC_OBJECT_TYPES_H_

// IWYU pragma: begin_exports
#include <tuple>
#include <array>
#include <cstdint>

#include "aether/types/span.h"
#include "aether/types/static_map.h"
// IWYU pragma: end_exports

namespace ae {
struct ObjectPathKey {
  bool operator==(ObjectPathKey const& right) const {
    return std::tie(obj_id, class_id, version) ==
           std::tie(right.obj_id, right.class_id, right.version);
  }

  std::uint32_t obj_id;
  std::uint32_t class_id;
  std::uint8_t version;
};

template <auto ObjectCount, auto ClassDataCount>
struct StaticDomainData {
  StaticMap<std::uint32_t, Span<std::uint32_t const>, ObjectCount> object_map;
  StaticMap<ObjectPathKey, Span<std::uint8_t const>, ClassDataCount> state_map;
};

template <std::size_t ObjectCount, std::size_t ClassDataCount>
StaticDomainData(
    StaticMap<std::uint32_t, Span<std::uint32_t const>, ObjectCount>&& om,
    StaticMap<ObjectPathKey, Span<std::uint8_t const>, ClassDataCount>&& sm)
    -> StaticDomainData<ObjectCount, ClassDataCount>;
}  // namespace ae

#endif  // AETHER_DOMAIN_STORAGE_STATIC_OBJECT_TYPES_H_
