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

#include "aether/events/event_list.h"

namespace ae {
EventHandlersList::IteratorAdapter::Iterator::Iterator(
    typename ListType::iterator it, std::uint16_t index, std::uint16_t size)
    : size_{size} {
  auto counter = index;
  for (auto i = it; counter < size_; ++i, ++counter) {
    if (i->is_alive) {
      it_ = i;
      index_ = counter;
      return;
    }
  }
  index_ = size_;
}
EventHandlersList::IteratorAdapter::Iterator&
EventHandlersList::IteratorAdapter::Iterator::operator++() {
  it_++;
  index_++;
  if (index_ == size_) {
    return *this;
  }
  if (!it_->is_alive) {
    return operator++();
  }
  return *this;
}

EventHandlersList::IteratorAdapter::Iterator
EventHandlersList::IteratorAdapter::Iterator::operator++(int) {
  Iterator tmp(*this);
  ++(*this);
  return tmp;
}

bool EventHandlersList::IteratorAdapter::Iterator::operator==(
    const Iterator& other) const {
  return index_ == other.index_;
}

bool EventHandlersList::IteratorAdapter::Iterator::operator!=(
    const Iterator& other) const {
  return !(*this == other);
}

EventHandlersList::IteratorAdapter::IteratorAdapter(EventHandlersList* self)
    : self_{self},
      begin_{std::begin(self_->handlers_), 0,
             static_cast<std::uint16_t>(self_->handlers_.size())},
      end_{std::end(self_->handlers_),
           static_cast<std::uint16_t>(self_->handlers_.size()),
           static_cast<std::uint16_t>(self_->handlers_.size())} {
  ++self_->use_counter_;
}

EventHandlersList::IteratorAdapter::~IteratorAdapter() {
  if ((--self_->use_counter_) == 0) {
    self_->CleanUp();
  }
}

EventHandlersList::IteratorAdapter EventHandlersList::Iterator() {
  auto lock = std::scoped_lock{lock_handlers_};
  return IteratorAdapter{this};
}

void EventHandlersList::Remove(Index index) {
  auto lock = std::scoped_lock{lock_handlers_};
  index.it->is_alive = false;
}

bool EventHandlersList::Alive(Index index) const {
  auto lock = std::scoped_lock{lock_handlers_};
  return index.it->is_alive;
}

void EventHandlersList::CleanUp() {
  auto lock = std::scoped_lock{lock_handlers_};
  if (use_counter_ != 0) {
    return;
  }
  // remove only from the end to prevent held indexes invalidation
  auto remove_begin = std::end(handlers_);
  auto it = std::find_if(std::rbegin(handlers_), std::rend(handlers_),
                         [](auto const& h) { return h.is_alive; });
  if (it != std::rend(handlers_)) {
    remove_begin = it.base();
  }
  handlers_.erase(remove_begin, std::end(handlers_));
}
}  // namespace ae
