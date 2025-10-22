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
#ifndef AETHER_MISC_FROM_CHARS_H_
#define AETHER_MISC_FROM_CHARS_H_

#include <charconv>
#include <optional>
#include <string_view>

namespace ae {
template <typename T>
std::optional<T> FromChars(std::string_view str, int base = 10) {
  T value;
  // skip 0x if present
  auto skip_pos = str.find("0x");
  if (skip_pos != std::string_view::npos) {
    str.remove_prefix(skip_pos + 2);
  }
  auto [ptr, ec] =
      std::from_chars(str.data(), str.data() + str.size(), value, base);
  if (ec == std::errc()) {
    return value;
  }
  return std::nullopt;
}
}  // namespace ae

#endif  // AETHER_MISC_FROM_CHARS_H_
