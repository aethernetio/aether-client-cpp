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

#include <utility>

#include "aether/common.h"
#include "aether/ptr/rc_ptr.h"

#include "aether/events/event_handler.h"

namespace ae {
struct SubscriptionManage {
  bool alive;
  bool once;
};

/**
 * \brief Event Handler subscription stored int Event<Signature> class.
 * handler_ is owned by Subscription \see Subscription.
 */
template <typename Signature>
class EventHandlerSubscription {
 public:
  explicit EventHandlerSubscription(
      RcPtr<SubscriptionManage> const& subscription,
      EventHandler<Signature>&& handler)
      : handler_{std::move(handler)}, subscription_{subscription} {}

  AE_CLASS_COPY_MOVE(EventHandlerSubscription)

  template <typename... TArgs>
  void Invoke(TArgs&&... args) {
    if (auto sub = subscription_.lock(); sub) {
      if (sub->alive) {
        handler_.Invoke(std::forward<TArgs>(args)...);
      }
      if (sub->once) {
        sub->alive = false;
      }
    }
  }

  bool is_alive() const {
    if (auto sub = subscription_.lock(); sub) {
      return sub->alive;
    }
    return false;
  }

  // call before remove handler
  void Reset() {
    if (auto sub = subscription_.lock(); sub) {
      sub->alive = false;
    }
  }

 private:
  EventHandler<Signature> handler_;
  RcPtrView<SubscriptionManage> subscription_;
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

  explicit Subscription(RcPtr<SubscriptionManage> subscription);

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
  RcPtr<SubscriptionManage> subscription_;
};

}  // namespace ae

#endif  // AETHER_EVENTS_EVENT_SUBSCRIPTION_H_
