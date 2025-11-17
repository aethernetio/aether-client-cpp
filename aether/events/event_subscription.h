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

#ifndef AETHER_EVENTS_EVENT_SUBSCRIPTION_H_
#define AETHER_EVENTS_EVENT_SUBSCRIPTION_H_

#include <optional>

#include "aether/events/event_deleter.h"

namespace ae {
/**
 * \brief RAII object to manage event subscription
 * It stores event handler deleter and call delete on destructor
 */
class Subscription {
 public:
  using EventHandler = EventHandlerDeleter;

  Subscription() = default;

  Subscription(EventHandlerDeleter&& event_handler_deleter);
  Subscription(Subscription const&) = delete;
  Subscription(Subscription&& other) noexcept;
  ~Subscription();

  Subscription& operator=(Subscription const&) = delete;
  Subscription& operator=(Subscription&& other) noexcept;
  Subscription& operator=(EventHandlerDeleter&& event_handler_deleter) noexcept;

  void Reset();

  explicit operator bool() const;

 private:
  std::optional<EventHandlerDeleter> event_handler_deleter_;
};

}  // namespace ae

#endif  // AETHER_EVENTS_EVENT_SUBSCRIPTION_H_
