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

#ifndef AETHER_OBJ_COMPONENT_CONTEXT_H_
#define AETHER_OBJ_COMPONENT_CONTEXT_H_

#include <memory>
#include <cstdint>
#include <utility>
#include <unordered_map>

#include "aether/common.h"
#include "aether/obj/obj.h"
#include "aether/type_traits.h"
#include "aether/types/small_function.h"

namespace ae {
template <typename TContext>
class ComponentContext {
 public:
  using TypeIndex = std::uint32_t;

  template <typename T, typename TFunc,
            AE_REQUIRERS((
                IsFunctor<TFunc, std::shared_ptr<T>(TContext const& context)>))>
  void Factory(TFunc&& factory) {
    factories_[T::kClassId] =
        [f{std::forward<TFunc>(factory)}](TContext const& context) mutable {
          return std::invoke(std::forward<TFunc>(f), context);
        };
  }

  template <typename T>
  std::shared_ptr<T> Resolve() const {
    static constexpr auto class_id = T::kClassId;
    // try cached component
    auto it = components_.find(class_id);
    if (it != components_.end()) {
      return std::static_pointer_cast<T>(it->second);
    }
    // try factory
    auto factory_it = factories_.find(class_id);
    if (factory_it == factories_.end()) {
      return {};
    }

    // create component and save it to factory
    auto component = factory_it->second(static_cast<TContext const&>(*this));
    components_[class_id] = component;
    return std::static_pointer_cast<T>(component);
  }

 private:
  std::unordered_map<
      TypeIndex, SmallFunction<std::shared_ptr<Obj>(TContext const& context)>>
      factories_;
  mutable std::unordered_map<TypeIndex, std::shared_ptr<Obj>> components_;
};
}  // namespace ae

#endif  // AETHER_OBJ_COMPONENT_CONTEXT_H_
