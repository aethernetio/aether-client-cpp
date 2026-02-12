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
Duration Uap::IntervalState::remaining() const {
  return std::chrono::duration_cast<Duration>(until() - Now());
}

TimePoint Uap::IntervalState::until() const {
  return start_time + interval.duration;
}

Uap::Timer::Timer(Uap::ptr uap) : uap_{std::move(uap)} {}

Uap::IntervalState Uap::Timer::interval(Duration time_offset) const {
  return uap_
      .WithLoaded(
          [&](auto const& uap) { return uap->UpdateInterval(time_offset); })
      .value_or(Uap::IntervalState{});
}

Uap::Uap() { start_time_ = Now(); }

Uap::Uap(ObjProp prop, ObjPtr<Aether> aether,
         std::initializer_list<Interval> const& interval_list)
    : Obj{prop},
      aether_{std::move(aether)},
      intervals_{std::begin(interval_list), std::end(interval_list)} {
  start_time_ = Now();
}

void Uap::SleepReady() {
  // TODO: add checks if all buffers are empty
  GoToSleep();
}

Uap::SleepEvent::Subscriber Uap::sleep_event() {
  return EventSubscriber{sleep_event_};
}

void Uap::SetIntervals(std::initializer_list<Interval> const& interval_list) {
  intervals_ =
      std::vector<Interval>{std::begin(interval_list), std::end(interval_list)};
  // update indices
  current_interval_index_ = current_interval_index_ % intervals_.size();
  next_interval_index_ = (current_interval_index_ + 1) % intervals_.size();
}

std::optional<Uap::Timer> Uap::timer() {
  if (intervals_.empty()) {
    return std::nullopt;
  }
  return Timer{Uap::ptr::MakeFromThis(this)};
}

Uap::IntervalState Uap::UpdateInterval(Duration time_offset) {
  assert(!intervals_.empty());

  next_interval_index_ = (current_interval_index_ + 1) % intervals_.size();
  auto interval_duration = intervals_[current_interval_index_].duration;
  auto current_time = Now() + time_offset;
  auto time_elapsed =
      std::chrono::duration_cast<Duration>(current_time - start_time_);
  if (time_elapsed > interval_duration) {
    AE_TELED_WARNING("!More time elapsed than current interval {:%S} > {%S}",
                     time_elapsed, interval_duration);
    // find current interval index and new interval duration
    std::size_t index = next_interval_index_;
    auto interval_start = start_time_;
    while (time_elapsed > interval_duration) {
      // move all timers like it's new interval has started already
      interval_start += interval_duration;
      time_elapsed -= interval_duration;
      index = (index + 1) % intervals_.size();
      interval_duration = intervals_[index].duration;
    }
    start_time_ = interval_start;
    current_interval_index_ = index;
    next_interval_index_ = (index + 1) % intervals_.size();
  }
  AE_TELED_DEBUG(
      "Current interval {}, next interval {}, start_time {:%H:%M:%S}",
      current_interval_index_, next_interval_index_, start_time_);
  return IntervalState{intervals_[current_interval_index_], start_time_};
}

void Uap::GoToSleep() {
  if (intervals_.empty()) {
    return;
  }
  sleep_event_.Emit(Timer{Uap::ptr::MakeFromThis(this)});
}
}  // namespace ae
