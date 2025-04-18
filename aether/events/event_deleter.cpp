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

#include "aether/events/event_deleter.h"

namespace ae {
EventHandlerDeleter::EventHandlerDeleter(EventHandlerDeleter const& other)
    : storage_{other.storage_},
      deleter_{
          storage_
              .template ptr<events_internal::EventHandlerDeleterImplBase>()} {}

EventHandlerDeleter::EventHandlerDeleter(EventHandlerDeleter&& other) noexcept
    : storage_{std::move(other.storage_)},
      deleter_{
          storage_
              .template ptr<events_internal::EventHandlerDeleterImplBase>()} {}

EventHandlerDeleter& EventHandlerDeleter::operator=(
    EventHandlerDeleter const& other) {
  if (this != &other) {
    storage_ = other.storage_;
    deleter_ =
        storage_.template ptr<events_internal::EventHandlerDeleterImplBase>();
  }
  return *this;
}

EventHandlerDeleter& EventHandlerDeleter::operator=(
    EventHandlerDeleter&& other) noexcept {
  if (this != &other) {
    storage_ = std::move(other.storage_);
    deleter_ =
        storage_.template ptr<events_internal::EventHandlerDeleterImplBase>();
  }
  return *this;
}

}  // namespace ae
