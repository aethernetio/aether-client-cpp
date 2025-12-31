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

#include "aether/events/multi_subscription.h"

namespace ae {
MultiSubscription::~MultiSubscription() { Reset(); }

void MultiSubscription::Reset() {
  for (auto& del : deleters_) {
    if (del.alive()) {
      del.Delete();
    }
  }
  deleters_.clear();
}

void MultiSubscription::CleanUp() {
  deleters_.erase(std::remove_if(std::begin(deleters_), std::end(deleters_),
                                 [](auto const& del) { return !del.alive(); }),
                  std::end(deleters_));
}

MultiSubscription& MultiSubscription::operator+=(
    EventHandlerDeleter&& deleter) {
  CleanUp();
  PushToVector(std::move(deleter));
  return *this;
}

void MultiSubscription::PushToVector(EventHandlerDeleter&& deleter) {
  deleters_.emplace_back(std::move(deleter));
}

std::size_t MultiSubscription::count() const { return deleters_.size(); }

}  // namespace ae
