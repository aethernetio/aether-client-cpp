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

#include "aether/events/event_subscription.h"

#include <cassert>

namespace ae {

EventHandlerSubscription::EventHandlerSubscription(
    std::shared_ptr<IEventHandler> handler)
    : handler_(std::move(handler)) {}

EventHandlerSubscription::~EventHandlerSubscription() = default;

bool EventHandlerSubscription::is_alive() const {
  if (auto handler = handler_.lock(); handler) {
    return handler->is_alive();
  }
  return false;
}

void EventHandlerSubscription::Reset() {
  if (auto handler = handler_.lock(); handler) {
    handler->set_dead();
  }
}

Subscription::Subscription() = default;

Subscription::Subscription(std::shared_ptr<IEventHandler> handler)
    : handler_{std::move(handler)} {}

Subscription::Subscription(Subscription&& other) noexcept
    : handler_(std::move(other.handler_)) {}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
  Reset();
  handler_ = std::move(other.handler_);
  return *this;
}

Subscription::~Subscription() { Reset(); }

Subscription::operator bool() const { return handler_ && handler_->is_alive(); }

void Subscription::Reset() { handler_.reset(); }

Subscription Subscription::Once() && {
  assert(handler_);
  handler_->set_once();
  return std::move(*this);
}
}  // namespace ae
