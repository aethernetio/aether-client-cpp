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

#ifndef AETHER_TYPES_STATIC_MAP_H_
#define AETHER_TYPES_STATIC_MAP_H_

#include <array>
#include <cstddef>
#include <utility>

namespace ae {
template <typename K, typename T, std::size_t Size>
class StaticMap {
  template <typename U, std::size_t USize, std::size_t... Is>
  static constexpr auto GetArray(U const (&init)[USize],
                                 std::index_sequence<Is...>) {
    return std::array{init[Is]...};
  }

 public:
  using key_type = K;
  using mapped_type = T;
  using value_type = std::pair<key_type, mapped_type>;
  using size_type = std::size_t;
  using storage_type = std::array<value_type, Size>;

  constexpr explicit StaticMap(value_type const (&init)[Size])
      : storage_{GetArray(init, std::make_index_sequence<Size>())} {}

  constexpr explicit StaticMap(std::array<value_type, Size> init)
      : storage_{std::move(init)} {}

  [[nodiscard]] constexpr decltype(auto) begin() const {
    return std::begin(storage_);
  }
  [[nodiscard]] constexpr decltype(auto) cbegin() const {
    return std::cbegin(storage_);
  }
  [[nodiscard]] constexpr decltype(auto) rbegin() const {
    return std::rbegin(storage_);
  }
  [[nodiscard]] constexpr decltype(auto) crbegin() const {
    return std::crbegin(storage_);
  }
  [[nodiscard]] constexpr decltype(auto) end() const {
    return std::end(storage_);
  }
  [[nodiscard]] constexpr decltype(auto) cend() const {
    return std::cend(storage_);
  }
  [[nodiscard]] constexpr decltype(auto) rend() const {
    return std::rend(storage_);
  }
  [[nodiscard]] constexpr decltype(auto) crend() const {
    return std::crend(storage_);
  }

  constexpr bool empty() const { return Size == 0; }
  constexpr size_type size() const { return Size; }

  [[nodiscard]] constexpr decltype(auto) find(key_type const& key) const {
    for (std::size_t i = 0; i < Size; ++i) {
      if (storage_[i].first == key) {
        return std::next(
            std::begin(storage_),
            static_cast<typename storage_type::difference_type>(i));
      }
    }
    return std::end(storage_);
  }

 private:
  storage_type storage_;
};

template <typename K, typename T, std::size_t Size>
StaticMap(std::pair<K, T> (&arr)[Size]) -> StaticMap<K, T, Size>;
}  // namespace ae

#endif  // AETHER_TYPES_STATIC_MAP_H_
