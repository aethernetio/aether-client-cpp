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

#include "aether/events/events_mt.h"
#include "aether/events/event_deleter.h"

namespace ae {
/**
 * \brief RAII object to manage event subscription
 * It stores event handler deleter and call delete on destructor
 */
template <typename TSyncPolicy>
class BaseSubscription {
 public:
  BaseSubscription() = default;

  BaseSubscription(EventHandlerDeleter<TSyncPolicy>&& event_handler_deleter)
      : event_handler_deleter_{std::move(event_handler_deleter)} {}

  BaseSubscription(BaseSubscription const&) = delete;

  BaseSubscription(BaseSubscription&& other) noexcept
      : event_handler_deleter_{std::move(other.event_handler_deleter_)} {
    other.event_handler_deleter_.reset();
  }

  ~BaseSubscription() { Reset(); }

  BaseSubscription& operator=(BaseSubscription const&) = delete;

  BaseSubscription& operator=(BaseSubscription&& other) noexcept {
    if (this != &other) {
      Reset();
      event_handler_deleter_ = std::move(other.event_handler_deleter_);
      other.event_handler_deleter_.reset();
    }
    return *this;
  }

  BaseSubscription& operator=(
      EventHandlerDeleter<TSyncPolicy>&& event_handler_deleter) noexcept {
    Reset();
    event_handler_deleter_ = std::move(event_handler_deleter);
    return *this;
  }

  void Reset() {
    if (event_handler_deleter_) {
      event_handler_deleter_->Delete();
      event_handler_deleter_.reset();
    }
  }

  explicit operator bool() const {
    return event_handler_deleter_ && event_handler_deleter_->alive();
  }

 private:
  std::optional<EventHandlerDeleter<TSyncPolicy>> event_handler_deleter_;
};

/**
 * \brief Subscription for most cases.
 */
using Subscription = BaseSubscription<NoLockSyncPolicy>;

}  // namespace ae

#endif  // AETHER_EVENTS_EVENT_SUBSCRIPTION_H_
