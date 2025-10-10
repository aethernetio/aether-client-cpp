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

#ifndef AETHER_ACTIONS_ACTION_PTR_H_
#define AETHER_ACTIONS_ACTION_PTR_H_

#include <memory>

#include <type_traits>

#include "aether/common.h"
#include "aether/actions/action_context.h"

namespace ae {
template <typename T, typename Enable = void>
struct ActionStoppable : std::false_type {};

template <typename T>
struct ActionStoppable<T, std::void_t<decltype(std::declval<T&>().Stop())>>
    : std::true_type {};

/**
 * \brief Shared ownership to actions
 */
template <typename TAction>
class ActionPtr {
  template <typename UAction>
  friend class ActionPtr;

 public:
  ActionPtr() = default;

  template <typename... TArgs>
  explicit ActionPtr(ActionContext action_context, TArgs&&... args)
      : ptr_{std::make_shared<TAction>(action_context,
                                       std::forward<TArgs>(args)...)} {
    action_context.get_registry().PushBack(ptr_);
  }

  template <typename UAction, AE_REQUIRERS((std::is_base_of<TAction, UAction>))>
  ActionPtr(ActionPtr<UAction> const& other) : ptr_(other.ptr_) {}

  template <typename UAction, AE_REQUIRERS((std::is_base_of<TAction, UAction>))>
  ActionPtr(ActionPtr<UAction>&& other) noexcept
      : ptr_(std::move(other.ptr_)) {}

  AE_CLASS_COPY_MOVE(ActionPtr)

  template <typename UAction, AE_REQUIRERS((std::is_base_of<TAction, UAction>))>
  ActionPtr& operator=(ActionPtr<UAction> const& other) {
    ptr_ = other.ptr_;
    return *this;
  }

  template <typename UAction, AE_REQUIRERS((std::is_base_of<TAction, UAction>))>
  ActionPtr& operator=(ActionPtr<UAction>&& other) noexcept {
    ptr_ = std::move(other.ptr_);
    return *this;
  }

  TAction& operator*() { return *ptr_; }
  TAction const& operator*() const { return *ptr_; }
  TAction* operator->() const { return ptr_.get(); }
  explicit operator bool() const { return ptr_ != nullptr; }
  void reset() { ptr_.reset(); }

 private:
  std::shared_ptr<TAction> ptr_;
};

template <typename T>
ActionPtr(ActionPtr<T>&& t) -> ActionPtr<T>;
template <typename T>
ActionPtr(ActionPtr<T> const& t) -> ActionPtr<T>;

template <typename T, typename... TArgs>
auto MakeActionPtr(ActionContext action_context, TArgs&&... args) {
  return ActionPtr<T>(action_context, std::forward<TArgs>(args)...);
}

template <template <typename...> typename T, typename... TArgs>
auto MakeActionPtr(ActionContext action_context, TArgs&&... args) {
  using DeducedT = decltype(T(action_context, std::forward<TArgs>(args)...));
  return ActionPtr<DeducedT>(action_context, std::forward<TArgs>(args)...);
}

/**
 * \brief Ownership to actions. Calls Action::Stop on destruction.
 */
template <typename TAction>
class OwnActionPtr : public ActionPtr<TAction> {
 public:
  template <typename UAction>
  friend class OwnActionPtr;

  static_assert(ActionStoppable<TAction>::value, "TAction must be stoppable");
  OwnActionPtr() = default;

  template <typename... TArgs>
  explicit OwnActionPtr(ActionContext action_context, TArgs&&... args)
      : ActionPtr<TAction>{action_context, std::forward<TArgs>(args)...} {}

  template <typename UAction, AE_REQUIRERS((std::is_base_of<TAction, UAction>))>
  OwnActionPtr(OwnActionPtr<UAction>&& other) noexcept
      : ActionPtr<TAction>{std::move(other)} {}

  ~OwnActionPtr() { reset(); }

  AE_CLASS_MOVE_ONLY(OwnActionPtr)

  template <typename UAction, AE_REQUIRERS((std::is_base_of<TAction, UAction>))>
  OwnActionPtr& operator=(OwnActionPtr<UAction>&& other) noexcept {
    ActionPtr<TAction>::operator=(std::move(other));
    return *this;
  }

  using ActionPtr<TAction>::operator*;
  using ActionPtr<TAction>::operator->;
  using ActionPtr<TAction>::operator bool;

  operator ActionPtr<TAction>() {
    return static_cast<ActionPtr<TAction>>(*this);
  }

  void reset() {
    if (ActionPtr<TAction>::operator bool()) {
      if (!ActionPtr<TAction>::operator->() -> IsFinished()) {
        ActionPtr<TAction>::operator->() -> Stop();
      }
    }
    ActionPtr<TAction>::reset();
  }
};

template <typename T>
OwnActionPtr(OwnActionPtr<T>&& t) -> OwnActionPtr<T>;
template <typename T>
OwnActionPtr(OwnActionPtr<T> const& t) -> OwnActionPtr<T>;

}  // namespace ae

#endif  // AETHER_ACTIONS_ACTION_PTR_H_
