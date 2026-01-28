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

#ifndef AETHER_OBJ_COMPONENT_FACTORY_H_
#define AETHER_OBJ_COMPONENT_FACTORY_H_

#include <optional>

#include "aether/common.h"
#include "aether/type_traits.h"
#include "aether/types/small_function.h"

namespace ae {
template <typename... T>
class ComponentFactory;

template <typename T>
class ComponentFactory<T> {
 public:
  using ObjFactory = SmallFunction<T()>;

  ComponentFactory() = default;

  /**
   * \brief Set the object factory to create the object on resolve.
   */
  template <typename F, AE_REQUIRERS((IsFunctor<F, T()>))>
  void Factory(F&& factory) {
    factory_.emplace(std::forward<F>(factory));
  }

  /**
   * \brief Set the object itself to use it on resolve.
   */
  void Set(T&& obj) { obj_.emplace(std::move(obj)); }

  /**
   * \brief Resolve the object. Return if it's valid, or create of factory is
   * set.
   */
  T& Resolve() const {
    if (obj_.has_value()) {
      return obj_.value();
    }
    assert(!!factory_ && "Neither object nor factory is set");
    obj_.emplace(factory_.value()());
    return obj_.value();
  }

  explicit operator bool() const {
    return obj_.has_value() || factory_.has_value();
  }

 private:
  std::optional<ObjFactory> factory_;
  mutable std::optional<T> obj_;
};

template <typename TContext, typename T>
class ComponentFactory<TContext, T> {
 public:
  using ObjFactory = SmallFunction<T(TContext const& context)>;

  ComponentFactory() = default;

  /**
   * \brief Set the object factory to create the object on resolve.
   */
  template <typename F,
            AE_REQUIRERS((IsFunctor<F, T(TContext const& context)>))>
  void Factory(F&& factory) {
    factory_.emplace(std::forward<F>(factory));
  }

  /**
   * \brief Set the object itself to use it on resolve.
   */
  void Set(T&& obj) { obj_.emplace(std::move(obj)); }

  /**
   * \brief Resolve the object. Return if it's valid, or create of factory is
   * set.
   */
  T& Resolve(TContext const& context) const {
    if (obj_.has_value()) {
      return obj_.value();
    }
    assert(!!factory_ && "Neither object nor factory is set");
    obj_.emplace(factory_.value()(context));
    return obj_.value();
  }

  explicit operator bool() const {
    return obj_.has_value() || factory_.has_value();
  }

 private:
  std::optional<ObjFactory> factory_;
  mutable std::optional<T> obj_;
};
}  // namespace ae

#endif  // AETHER_OBJ_COMPONENT_FACTORY_H_
