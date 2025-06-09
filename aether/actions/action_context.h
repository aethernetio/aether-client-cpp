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

#ifndef AETHER_ACTIONS_ACTION_CONTEXT_H_
#define AETHER_ACTIONS_ACTION_CONTEXT_H_

#include "aether/actions/action_registry.h"
#include "aether/actions/action_trigger.h"

namespace ae {
namespace action_internal {
template <typename T, typename Enab = void>
struct ActionContextConcept : std::false_type {};

template <typename T>
struct ActionContextConcept<
    T, std::void_t<decltype(std::declval<T>().get_trigger()),
                   decltype(std::declval<T>().get_registry())>>
    : std::true_type {};
}  // namespace action_internal

class ActionContext {
 public:
  template <typename T, AE_REQUIRERS(action_internal::ActionContextConcept<T>),
            AE_REQUIRERS_NOT((std::is_same<ActionContext, std::decay_t<T>>))>
  ActionContext(T&& context)
      : trigger_{&context.get_trigger()}, registry_{&context.get_registry()} {}

  ActionTrigger& get_trigger() { return *trigger_; }
  ActionRegistry& get_registry() { return *registry_; }

 private:
  ActionTrigger* trigger_;
  ActionRegistry* registry_;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_ACTION_CONTEXT_H_
