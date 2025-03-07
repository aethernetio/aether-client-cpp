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

#ifndef AETHER_MEMORY_H_
#define AETHER_MEMORY_H_

#include <memory>

namespace ae {
using std::make_unique;

template <template <typename...> typename T, typename... TArgs>
auto make_unique(TArgs&&... args) {
  using Deduced = decltype(T(std::forward<TArgs>(args)...));
  return make_unique<Deduced>(std::forward<TArgs>(args)...);
}

}  // namespace ae

#endif  // AETHER_MEMORY_H_
