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

#ifndef AETHER_ACTIONS_ACTION_VIEW_H_
#define AETHER_ACTIONS_ACTION_VIEW_H_

#include "aether/actions/action.h"  // IWYU pragma: keep
#include "aether/actions/action_registry.h"

namespace ae {
/**
 * \brief View to action registered in ActionRegistry
 */
template <typename T>
class ActionView {
  template <typename U>
  friend class ActionView;

 public:
  ActionView() = default;
  ActionView(T const& action) : index_{action.index()} {}

  ActionView(ActionView const& other) : index_{other.index_} {}
  ActionView(ActionView&& other) noexcept : index_{std::move(other.index_)} {}

  template <typename U, std::enable_if_t<std::is_base_of_v<T, U>, int> = 0>
  ActionView(ActionView<U> const& other) : index_{other.index_} {}

  template <typename U, std::enable_if_t<std::is_base_of_v<T, U>, int> = 0>
  ActionView(ActionView<U>&& other) noexcept
      : index_{std::move(other.index_)} {}

  ActionView& operator=(ActionView const& other) {
    if (this != &other) {
      index_ = other.index_;
    }
    return *this;
  }

  ActionView& operator=(ActionView&& other) noexcept {
    if (this != &other) {
      index_ = std::move(other.index_);
    }
    return *this;
  }

  template <typename U, std::enable_if_t<std::is_base_of_v<T, U>, int> = 0>
  ActionView& operator=(ActionView<U> const& other) {
    index_ = other.index_;
    return *this;
  }

  template <typename U, std::enable_if_t<std::is_base_of_v<T, U>, int> = 0>
  ActionView& operator=(ActionView<U>&& other) noexcept {
    index_ = std::move(other.index_);
    return *this;
  }

  /**
   * \brief Check if action is alive.
   */
  explicit operator bool() const { return index_.get() != nullptr; }

  /**
   * \brief Get action reference from registry.
   * get UB if action is not alive anymore. \see operator bool
   */
  auto const& operator*() const { return static_cast<T&>(*index_.get()); }
  auto& operator*() { return static_cast<T&>(*index_.get()); }

  /**
   * \brief Get access to action
   */

  auto const* operator->() const { return static_cast<T const*>(index_.get()); }
  auto* operator->() { return static_cast<T*>(index_.get()); }

 private:
  ActionRegistry::IndexShare index_;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_ACTION_VIEW_H_
