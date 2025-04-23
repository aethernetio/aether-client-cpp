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

#ifndef AETHER_EVENTS_BARRIER_EVENT_H_
#define AETHER_EVENTS_BARRIER_EVENT_H_

#include <array>
#include <utility>
#include <cassert>
#include <algorithm>

#include "aether/events/events.h"

namespace ae {
// TODO: add tests

/**
 * \brief Accumulates data from other events and emit self after all of them.
 */
template <typename T, std::size_t Count>
class BarrierEvent {
 public:
  template <typename TFunc>
  auto Subscribe(TFunc&& func) {
    return EventSubscriber{res_event_}.Subscribe(std::forward<TFunc>(func));
  }

  template <std::size_t I>
  void Emit(T t) {
    values_[I] = std::move(t);
    set_map_[I] = true;
    if (IsFull()) {
      res_event_.Emit(*this);
    }
  }

  void EmitNext(T t) {
    assert(current_index_ < Count);
    values_[current_index_] = std::move(t);
    set_map_[current_index_] = true;

    ++current_index_;
    if (IsFull()) {
      res_event_.Emit(*this);
    }
  }

  template <std::size_t I>
  T const& Get() const {
    return values_[I];
  }

  [[nodiscard]] auto begin() const { return std::begin(values_); }
  [[nodiscard]] auto end() const { return std::end(values_); }

  constexpr auto size() const { return values_.size(); }

 private:
  bool IsFull() {
    return std::all_of(std::begin(set_map_), std::end(set_map_),
                       [](auto v) { return v; });
  }

  Event<void(BarrierEvent const& barrier)> res_event_;
  std::size_t current_index_ = 0;
  std::array<bool, Count> set_map_{};
  std::array<T, Count> values_;
};

/**
 * \brief Specialization for void
 */
template <std::size_t Count>
class BarrierEvent<void, Count> {
 public:
  template <typename TFunc>
  auto Subscribe(TFunc&& func) {
    return EventSubscriber{res_event_}.Subscribe(std::forward<TFunc>(func));
  }

  template <std::size_t I>
  void Emit() {
    set_map_[I] = true;
    if (IsFull()) {
      res_event_.Emit();
    }
  }

  void EmitNext() {
    assert(current_index_ < Count);
    set_map_[current_index_] = true;

    ++current_index_;
    if (IsFull()) {
      res_event_.Emit();
    }
  }

 private:
  bool IsFull() {
    return std::all_of(std::begin(set_map_), std::end(set_map_),
                       [](auto v) { return v; });
  }

  Event<void()> res_event_;
  std::size_t current_index_ = 0;
  std::array<bool, Count> set_map_{};
};

}  // namespace ae

#endif  // AETHER_EVENTS_BARRIER_EVENT_H_
