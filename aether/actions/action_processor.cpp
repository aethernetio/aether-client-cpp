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

#include "aether/actions/action_processor.h"

#include <algorithm>

#include "aether/actions/action.h"  // IWYU pragma: keep

namespace ae {
TimePoint ActionProcessor::Update(TimePoint current_time) {
  auto next_update = current_time;

  for (auto const& action : ActionRegistryReplica()) {
    auto new_time = action->ActionUpdate(current_time);
    next_update = SelectNextUpdate(new_time, next_update, current_time);
  }
  CleanUpActions();
  return next_update;
}

ActionTrigger& ActionProcessor::get_trigger() { return action_trigger_; }

ActionRegistry& ActionProcessor::get_registry() { return action_registry_; }

TimePoint ActionProcessor::SelectNextUpdate(TimePoint new_time,
                                            TimePoint old_time,
                                            TimePoint current_time) {
  if (new_time > current_time) {
    if (old_time != current_time) {
      return std::min(new_time, old_time);
    }
    return new_time;
  }

  return std::max(old_time, current_time);
}

std::vector<ActionRegistry::value_type>
ActionProcessor::ActionRegistryReplica() {
  std::vector<ActionRegistry::value_type> result;
  result.reserve(action_registry_.size());
  for (auto const& a : action_registry_) {
    result.emplace_back(a);
  }
  return result;
}

void ActionProcessor::CleanUpActions() {
  for (auto it = std::begin(action_registry_);
       it != std::end(action_registry_);) {
    if ((*it)->IsFinished()) {
      it = action_registry_.Remove(it);
    } else {
      ++it;
    }
  }
}

}  // namespace ae
