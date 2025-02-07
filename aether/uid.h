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

#ifndef AETHER_UID_H_
#define AETHER_UID_H_

#include <array>
#include <cstdint>

namespace ae {

struct Uid;
struct Uid {
  static constexpr std::size_t kSize = 16;
  static const Uid kAether;

  explicit constexpr Uid(std::array<std::uint8_t, kSize> uid) : value(uid) {}
  Uid() = default;

  template <typename T>
  void Serializator(T& s) {
    s & value;
  }

  bool operator<(const Uid& rhs) const { return value < rhs.value; }
  bool operator==(const Uid& rhs) const { return value == rhs.value; }
  bool operator!=(const Uid& rhs) const { return value != rhs.value; }

  std::array<std::uint8_t, kSize> value;
};

}  // namespace ae

#endif  // AETHER_UID_H_ */
