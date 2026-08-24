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

#ifndef AETHER_PACKED_SIZE_H_
#define AETHER_PACKED_SIZE_H_

#include <cstdint>

#include "ae-numeric/tiered_int.h"

namespace ae {

// Canonical legacy-compatible wire size encoding used by packet and message
// framing. Byte-for-byte equivalent of the old
// TieredInt<std::uint64_t, std::uint8_t, 250>.
using PackedSize = TieredInt<std::uint8_t, 250, 1514, 1049834>;

// Transport framing uses the same wire encoding; keep the historical name as
// an alias so packet code stays readable without duplicating the type.
using PacketSize = PackedSize;

}  // namespace ae

#endif  // AETHER_PACKED_SIZE_H_
