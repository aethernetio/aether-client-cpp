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

#include "aether/uid.h"

#include <cassert>

namespace ae {

const Uid Uid::kAether = Uid{{1}};

Uid Uid::FromString(std::string_view str) {
  Uid uid;
  std::size_t index = 0;
  assert(str.size() == kUidStringSize);
  for (std::size_t i = 0; i < str.size();) {
    if (str[i] == '-') {
      i++;
      continue;
    }
    i += 2;
    uid.value[index++] =
        static_cast<std::uint8_t>(_internal::HexToDec(str.data() + i, 2));
  }
  return uid;
}
}  // namespace ae
