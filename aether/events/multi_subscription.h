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

#include "aether/events/event_subscription.h"

namespace ae {
/**
 * \brief RAII object to manage set of subscriptions
 */
class MultiSubscription {
 public:
  MultiSubscription() = default;
  ~MultiSubscription() = default;

  AE_CLASS_MOVE_ONLY(MultiSubscription)

  /**
   * \brief Push as many as you need subscriptions to the list.
   * All dead subscriptions will be cleaned up before add new.
   */
  template <typename... TSubscriptions>
  void Push(TSubscriptions&&... subscriptions) {
    CleanUp();
    (PushToVector(std::forward<TSubscriptions>(subscriptions)), ...);
  }

  void Reset();

 private:
  void CleanUp();
  void PushToVector(Subscription&& subscription);

  std::vector<Subscription> subscriptions_;
};
}  // namespace ae

#endif  // AETHER_EVENTS_MULTI_SUBSCRIPTION_H_
