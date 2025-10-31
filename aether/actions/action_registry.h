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

#ifndef AETHER_ACTIONS_ACTION_REGISTRY_H_
#define AETHER_ACTIONS_ACTION_REGISTRY_H_

#include <list>
#include <memory>

namespace ae {
class IAction;

class ActionRegistry {
 public:
  using ActionList = std::list<std::shared_ptr<IAction>>;
  using value_type = ActionList::value_type;

  ActionRegistry();

  /**
   * \brief Adds new action at the list
   */
  void PushBack(std::shared_ptr<IAction> action);

  [[nodiscard]] decltype(auto) begin() noexcept { return action_list_.begin(); }
  [[nodiscard]] decltype(auto) end() noexcept { return action_list_.end(); }
  [[nodiscard]] auto size() const noexcept { return action_list_.size(); }

  /**
   * \brief Remove pos element.
   * \return iterator to new element on that pos or end if registry is empty
   */
  ActionList::iterator Remove(ActionList::iterator pos);

 private:
  ActionList action_list_;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_ACTION_REGISTRY_H_
