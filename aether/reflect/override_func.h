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

#ifndef AETHER_REFLECT_OVERRIDE_FUNC_H_
#define AETHER_REFLECT_OVERRIDE_FUNC_H_

#include <utility>

namespace ae::reflect {
/**
 * \brief Functor overridable with many other functors.
 * Has a cumulative operator() of all Fs functors.
 */
template <typename... Fs>
struct OverrideFunc : Fs... {
  explicit OverrideFunc(Fs... funcs) : Fs{std::forward<Fs>(funcs)}... {}

  using Fs::operator()...;
};
template <typename... U>
OverrideFunc(U&&...) -> OverrideFunc<U...>;

}  // namespace ae::reflect

#endif  // AETHER_REFLECT_OVERRIDE_FUNC_H_
