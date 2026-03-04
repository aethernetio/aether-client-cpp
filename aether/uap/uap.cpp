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

#include <utility>
#include <numeric>

#include "aether/aether.h"
#include "aether/actions/timer_action.h"

#include "aether/tele/tele.h"

namespace ae {
Duration Uap::IntervalState::remaining() const {
  auto interval_end = until();
  auto current_time = SyncTime();
  auto diff = interval_end - current_time;
  AE_TELED_DEBUG(
      "Calculate remaining end {:%Y-%m-%d %H:%M:%S} current {:%Y-%m-%d "
      "%H:%M:%S} diff {:%S}",
      interval_end, current_time, diff);

  return std::chrono::duration_cast<Duration>(diff);
}

SyncTimePoint Uap::IntervalState::until() const { return end_time; }

Uap::Timer::Timer(Uap::ptr uap) : uap_{std::move(uap)} {}

Uap::IntervalState Uap::Timer::interval(Duration time_offset) const {
  return uap_
      .WithLoaded(
          [&](auto const& uap) { return uap->UpdateInterval(time_offset); })
      .value_or(Uap::IntervalState{});
}

Uap::Uap() { start_time_ = SyncTime(); }

Uap::Uap(ObjProp prop, ObjPtr<Aether> aether,
         std::initializer_list<Interval> const& interval_list)
    : Obj{prop},
      aether_{std::move(aether)},
      intervals_{std::begin(interval_list), std::end(interval_list)} {
  start_time_ = SyncTime();
  WindowWatcher();
}

Uap::~Uap() = default;

void Uap::SleepReady() {
  ready_to_sleep_ = true;
  // if all registered actions is finished else wait /see RegisterAction
  if (wait_actions_cnt_ == 0) {
    AllActionsFinished();
  }
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

void Uap::RegisterAction(IAction& action) {
  wait_actions_cnt_++;
  wait_actions_subs_ += action.FinishedEvent().Subscribe([this]() {
    assert(wait_actions_cnt_ > 0);
    wait_actions_cnt_--;
    if (wait_actions_cnt_ == 0) {
      AllActionsFinished();
    }
  });
}

void Uap::Loaded() { WindowWatcher(); }

void Uap::GoToSleep() {
  if (intervals_.empty()) {
    return;
  }
  sleep_event_.Emit(Timer{Uap::ptr::MakeFromThis(this)});
}

Uap::IntervalState Uap::UpdateInterval(Duration time_offset) {
  assert(!intervals_.empty());

  next_interval_index_ = (current_interval_index_ + 1) % intervals_.size();
  auto interval_duration = intervals_[current_interval_index_].duration;
  auto current_time = SyncTime() + time_offset;
  auto time_elapsed = current_time - start_time_;
  AE_TELED_DEBUG(
      "Update interval\n start_time {:%Y-%m-%d %H:%M:%S}\n current_time "
      "{:%Y-%m-%d %H:%M:%S}\n interval_duration {:%H:%M:%S}\n time_elapsed "
      "{}",
      start_time_, current_time, interval_duration, time_elapsed);

  if (time_elapsed > interval_duration) {
    AE_TELED_WARNING(
        "!More time elapsed than current interval {} > "
        "{:%H:%M:%S}",
        time_elapsed, interval_duration);

    std::size_t index = next_interval_index_;
    auto interval_start = start_time_;

    // test for bad case - if time_elapsed is more than several uap durations
    // find current interval index and new interval duration
    auto uap_duration = std::accumulate(
        std::begin(intervals_), std::end(intervals_), Duration{},
        [](auto v, auto const& i) { return v + i.duration; });
    // count how many full uap durations have elapsed and remove it from
    // time_elapsed
    auto uap_count = static_cast<std::uint32_t>(time_elapsed / uap_duration);
    time_elapsed -= uap_duration * uap_count;
    interval_start += uap_duration * uap_count;
    index = (index + uap_count * intervals_.size()) % intervals_.size();

    // find the new interval
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
      "Current interval {}, next interval {}, start_time {:%Y-%m-%d %H:%M:%S} "
      "current_time {:%Y-%m-%d %H:%M:%S}",
      current_interval_index_, next_interval_index_, start_time_, current_time);
  return IntervalState{
      .interval = intervals_[current_interval_index_],
      .end_time = start_time_ + intervals_[current_interval_index_].duration,
  };
}

void Uap::WindowWatcher() {
  if (intervals_.empty()) {
    return;
  }
  auto& current = intervals_[current_interval_index_];
  if (current.window == Duration::zero()) {
    return;
  }

  aether_.WithLoaded([this, w{current.window}](auto const& a) {
    auto timer_action = ActionPtr<TimerAction>{*a, w};
    RegisterAction(*timer_action);
  });
}

void Uap::AllActionsFinished() {
  AE_TELED_DEBUG("All registered actions finished");
  if (ready_to_sleep_) {
    GoToSleep();
  }
}

}  // namespace ae
