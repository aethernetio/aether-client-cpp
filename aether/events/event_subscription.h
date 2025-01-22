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

#include <memory>
#include <utility>

#include "aether/common.h"

#include "aether/events/event_handler.h"

namespace ae {
/**
 * \brief Event Handler subscription stored int Event<Signature> class.
 * handler_ is owned by Subscription \see Subscription.
 */
class EventHandlerSubscription {
 public:
  explicit EventHandlerSubscription(std::shared_ptr<IEventHandler> handler);

  ~EventHandlerSubscription();

  AE_CLASS_COPY_MOVE(EventHandlerSubscription)

  template <typename... TArgs>
  void invoke(TArgs&&... args) {
    using HandlerType = EventHandler<void(TArgs...)>;
    if (auto handler = handler_.lock(); handler) {
      if (handler->is_alive()) {
        static_cast<HandlerType&>(*handler).invoke(
            std::forward<TArgs>(args)...);
      }
    }
  }

  bool is_alive() const;

  // call before remove handler
  void Reset();

 private:
  // IEventHandler used to reduce shared_ptr template instantiations
  std::weak_ptr<IEventHandler> handler_;
};

/**
 * \brief RAII object to manage event subscription
 * It stores event handler and releases it on destruction
 * Call Once() to make event handler been called only once and then marked as
 * dead.
 */
class Subscription {
 public:
  Subscription();

  explicit Subscription(std::shared_ptr<IEventHandler> handler);

  Subscription(Subscription const&) = delete;
  Subscription(Subscription&& other) noexcept;

  ~Subscription();

  explicit operator bool() const;

  Subscription& operator=(Subscription const&) = delete;
  Subscription& operator=(Subscription&& other) noexcept;

  // Mark it as invoke only once
  Subscription Once() &&;

  void Reset();

 private:
  std::shared_ptr<IEventHandler> handler_;
};

}  // namespace ae

#endif  // AETHER_EVENTS_EVENT_SUBSCRIPTION_H_
