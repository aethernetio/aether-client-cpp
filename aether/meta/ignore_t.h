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

#ifndef AETHER_META_IGNORE_T_H_
#define AETHER_META_IGNORE_T_H_

#include <type_traits>

namespace ae {
struct Ignore {
  template <typename... Us>
  constexpr explicit Ignore(Us&&...) noexcept {}
};

template <typename T>
struct IsIgnore : std::false_type {};

template <>
struct IsIgnore<Ignore> : std::true_type {};
template <typename T>
static constexpr inline bool IsIgnore_v = IsIgnore<T>::value;
}  // namespace ae

#endif  // AETHER_META_IGNORE_T_H_
