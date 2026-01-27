/*
 * Copyright 2026 Aethernet Inc.
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

#include "aether/uap/uap.h"

#include "aether/aether.h"

#include "aether/tele/tele.h"

namespace ae {
Uap::Uap(ObjProp prop, ObjPtr<Aether> aether)
    : Obj{prop}, aether_{std::move(aether)} {}

void Uap::SleepReady() {
  // TODO: add checks if all buffers are empty
  GoToSleep();
}

Uap::SleepEvent::Subscriber Uap::sleep_event() {
  return EventSubscriber{sleep_event_};
}

void Uap::SetIntervals(std::initializer_list<Interval> const& interval_list) {
  intervals_.reserve(interval_list.size());
  intervals_.insert(intervals_.end(), interval_list.begin(),
                    interval_list.end());
  UpdateTimer();
}

void Uap::UpdateTimer() {
  if (intervals_.empty()) {
    return;
  }
  next_interval_index_ = (current_interval_index_ + 1) % intervals_.size();
  update_time_ = Now();
}

void Uap::GoToSleep() {
  if (intervals_.empty()) {
    return;
  }
  auto interval_duration = intervals_[current_interval_index_].duration;
  auto current_time = Now();
  auto time_elapsed =
      std::chrono::duration_cast<Duration>(current_time - update_time_);
  if (time_elapsed > interval_duration) {
    // find current interval index and new interval duration
    std::size_t index = next_interval_index_;
    while (time_elapsed > interval_duration) {
      time_elapsed -= interval_duration;
      index = (index + 1) % intervals_.size();
      interval_duration = intervals_[index].duration;
    }
    current_interval_index_ = index;
    next_interval_index_ = (index + 1) % intervals_.size();
  }

  auto sleep_until = current_time + interval_duration - time_elapsed;

  AE_TELED_DEBUG("Sleep until {:%Y-%m-%d %H:%M:%S} for interval {}",
                 sleep_until, current_interval_index_);
  sleep_event_.Emit(sleep_until);
}
}  // namespace ae
