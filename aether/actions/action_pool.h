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

#ifndef AETHER_ACTIONS_ACTION_POLL_H_
#define AETHER_ACTIONS_ACTION_POLL_H_

#include <variant>
#include <type_traits>

#include "aether/warning_disable.h"

DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include "third_party/etl/include/etl/pool.h"
#include "third_party/etl/include/etl/variant_pool.h"
DISABLE_WARNING_POP()

#include "aether/actions/action.h"
#include "aether/actions/action_context.h"

namespace ae {
template <ActionContext AC, typename T, std::size_t Capacity>
class ActionPool : public etl::pool<T, Capacity> {
 public:
  static_assert(std::is_base_of_v<Action, T>, "T must be a subclass of Action");

  using base_t = etl::pool<T, Capacity>;

  constexpr explicit ActionPool(AC const& ac) noexcept : ac_{ac} {}
  ~ActionPool() noexcept {
    for (auto i = base_t::begin(); i != base_t::end(); ++i) {
      base_t::destroy(&i.template get<T>());
    }
  }

  template <typename... Args>
  T* Create(Args&&... args) {
    auto* p = base_t::create(std::forward<Args>(args)...);
    if (p != nullptr) {
      // destroy on finish
      static_cast<Action*>(p)->finished_event().Subscribe(
          [this, p]() { Destroy(p); });
    }
    return p;
  }

 private:
  void Destroy(T* p) {
    ac_.scheduler().Task([&, p]() { base_t::template destroy<T>(p); });
  }

  AC ac_;
};

template <ActionContext AC, typename... T, std::size_t Capacity>
class ActionPool<AC, std::variant<T...>, Capacity>
    : public etl::variant_pool<Capacity, T...> {
 public:
  static_assert((std::is_base_of_v<Action, T> && ...),
                "T must be a subclass of Action");
  using base_t = etl::variant_pool<Capacity, T...>;

  constexpr explicit ActionPool(AC const& ac) noexcept : ac_{ac} {}

  template <typename U, typename... Args>
    requires(std::is_same_v<U, T> || ...)
  U* Create(Args&&... args) {
    auto* p = base_t::template create<U, Args...>(std::forward<Args>(args)...);
    if (p != nullptr) {
      // destroy on finish
      static_cast<Action*>(p)->finished_event().Subscribe(
          [this, p]() { Destroy<U>(p); });
    }
    return p;
  }

 private:
  template <typename U>
  void Destroy(U* p) {
    ac_.scheduler().Task([&, p]() { base_t::template destroy<U>(p); });
  }

  AC ac_;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_ACTION_POLL_H_
