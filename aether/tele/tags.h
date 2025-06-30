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

#ifndef AETHER_TELE_TAGS_H_
#define AETHER_TELE_TAGS_H_

#ifndef AETHER_TELE_TELE_H_
#  error "Include tele.h instead"
#endif

#include <cstdint>
#include <utility>
#include <string_view>

#include "aether/crc.h"
#include "aether/tele/modules.h"

namespace ae::tele {
namespace tags_internal {
template <auto Tag, std::int32_t N>
inline constexpr std::uint32_t kAeTagIndexCounter =
    kAeTagIndexCounter<Tag, N - 1>;
template <auto Tag>
inline constexpr std::uint32_t kAeTagIndexCounter<Tag, 0> =
    static_cast<std::uint32_t>(0 - 1);
}  // namespace tags_internal

// Telemetry tag
struct Tag {
  constexpr std::uint32_t index() const { return module.index_start + offset; }

  std::uint32_t offset;
  Module const& module;
  std::string_view name;
};
}  // namespace ae::tele

#define _AE_CRC(LITERAL) ::crc32::checksum_from_literal(LITERAL)

#define _AE_FILE_TAG(MODULE) _AE_CRC(__FILE__ #MODULE)
#define _AE_INDEX __LINE__
#define _AE_NEXT_INDEX (__LINE__ + 1)

#define _AE_TAG_INDEX_GET(MODULE) \
  ::ae::tele::tags_internal::kAeTagIndexCounter<_AE_FILE_TAG(MODULE), _AE_INDEX>

#define _AE_TAG_INDEX_WRITE(MODULE, VALUE)                             \
  template <>                                                          \
  inline constexpr auto ::ae::tele::tags_internal::kAeTagIndexCounter< \
      _AE_FILE_TAG(MODULE), _AE_NEXT_INDEX> = std::uint32_t{VALUE};

/**
 * \brief Register Tag with index specified
 */
#define AE_TAG_INDEXED(NAME, MODULE, INDEX)                                   \
  inline constexpr auto NAME = ::ae::tele::Tag{(INDEX), (MODULE), #NAME};     \
  _AE_TAG_INDEX_WRITE(MODULE, (NAME).offset)                                  \
  static_assert(((MODULE).index_start + (NAME).offset) <= (MODULE).index_end, \
                "Tag index out of range");

/**
 * \brief Register Tag with index automatically incremented from previous
 * AE_TAG* call
 */
#define AE_TAG(NAME, MODULE) \
  AE_TAG_INDEXED(NAME, MODULE, _AE_TAG_INDEX_GET(MODULE) + 1)
#endif  // AETHER_TELE_TAGS_H_
