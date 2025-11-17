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

#ifndef AETHER_EVENTS_MULTI_SUBSCRIPTION_H_
#define AETHER_EVENTS_MULTI_SUBSCRIPTION_H_

#include <vector>
#include <utility>

#include "aether/common.h"

#include "aether/events/event_deleter.h"

namespace ae {
/**
 * \brief RAII object to manage set of subscriptions
 */
class MultiSubscription {
 public:
  using EventHandler = EventHandlerDeleter;

  MultiSubscription() = default;
  ~MultiSubscription();

  AE_CLASS_MOVE_ONLY(MultiSubscription)

  /**
   * \brief Push as many as you need subscriptions to the list.
   * All dead subscriptions will be cleaned up before add new.
   */
  template <typename... TDeleters>
  void Push(TDeleters&&... deleters) {
    CleanUp();
    (PushToVector(std::forward<TDeleters>(deleters)), ...);
  }

  /**
   * \brief Reset all subscriptions.
   */
  void Reset();

 private:
  void CleanUp();

  void PushToVector(EventHandlerDeleter&& deleter);

  std::vector<EventHandlerDeleter> deleters_;
};

}  // namespace ae

#endif  // AETHER_EVENTS_MULTI_SUBSCRIPTION_H_
