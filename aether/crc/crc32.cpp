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

#include "aether/crc/crc32.h"

namespace ae {

static constexpr std::uint32_t kCRCPolynomial{0xEDB88320UL};
static constexpr std::uint32_t kCRCInit{0xFFFFFFFFUL};

std::uint32_t CRC32_function(DataBuffer data) {
  unsigned long crc_table[256];
  unsigned long crc;
  for (int i = 0; i < 256; i++) {
    crc = i;
    for (int j = 0; j < 8; j++)
      crc = crc & 1 ? (crc >> 1) ^ kCRCPolynomial : crc >> 1;
    crc_table[i] = crc;
  };
  crc = kCRCInit;

  for (auto i : data) {
    crc = crc_table[(crc ^ i) & 0xFF] ^ (crc >> 8);
  }

  return crc ^ kCRCInit;
}
}  // namespace ae
