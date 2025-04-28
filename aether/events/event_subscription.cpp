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

#include <utility>
#include <cassert>

namespace ae {
Subscription::Subscription() = default;

Subscription::Subscription(EventHandlerDeleter&& event_handler_deleter)
    : event_handler_deleter_{std::move(event_handler_deleter)} {}

Subscription& Subscription::operator=(
    EventHandlerDeleter&& event_handler_deleter) noexcept {
  Reset();
  event_handler_deleter_ = std::move(event_handler_deleter);
  return *this;
}

Subscription::Subscription(Subscription&& other) noexcept
    : event_handler_deleter_{std::move(other.event_handler_deleter_)} {
  other.event_handler_deleter_.reset();
}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
  Reset();
  event_handler_deleter_ = std::move(other.event_handler_deleter_);
  other.event_handler_deleter_.reset();
  return *this;
}

Subscription::~Subscription() { Reset(); }

void Subscription::Reset() {
  if (event_handler_deleter_) {
    event_handler_deleter_->Delete();
    event_handler_deleter_.reset();
  }
}

Subscription::operator bool() const {
  return event_handler_deleter_ && event_handler_deleter_->alive();
}
}  // namespace ae
