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

#ifndef EXAMPLES_AETHER_UAP_PEER_DEADLINE_TEST_COMMON_CRC32_H_
#define EXAMPLES_AETHER_UAP_PEER_DEADLINE_TEST_COMMON_CRC32_H_

#include <cstddef>
#include <cstdint>

namespace ae::test_uap_peer_deadline {

inline std::uint32_t Crc32(void const* data, std::size_t size) noexcept {
  auto const* bytes = static_cast<std::uint8_t const*>(data);
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (int b = 0; b < 8; ++b) {
      auto const mask =
          static_cast<std::uint32_t>(-(static_cast<int>(crc & 1u)));
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

}  // namespace ae::test_uap_peer_deadline

#endif  // EXAMPLES_AETHER_UAP_PEER_DEADLINE_TEST_COMMON_CRC32_H_
