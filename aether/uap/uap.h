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

#ifndef AETHER_UAP_UAP_H_
#define AETHER_UAP_UAP_H_

#include <vector>
#include <optional>

#include "aether/common.h"
#include "aether/obj/obj.h"
#include "aether/events/events.h"

namespace ae {
class Aether;

enum class IntervalType : char {
  /**
   * \brief Send only interval.
   * All the network staff should be created only for sending data.
   */
  kSendOnly,
  /**
   * \brief Receive only interval.
   * All the network staff should be created only for receiving data at this
   * activation window.
   */
  kReceiveOnly,
  /**
   * \brief Send and receive interval.
   * This activation window might be used for both sending and receiving data.
   */
  kSendReceive,
};

/**
 * \brief Basic interval type.
 * This shows the duration between two activation windows.
 * E.g. 10 seconds interval means the application should wake up, do its job,
 * and go to sleep for the time remaining from this 10 seconds.
 */
struct Interval {
  AE_REFLECT_MEMBERS(type, duration)

  IntervalType type;
  Duration duration;
};

class Uap final : public Obj {
  AE_OBJECT(Uap, Obj, 0)
  Uap();

  struct IntervalState {
    Interval interval;
    TimePoint start_time;

    Duration remaining() const;
    TimePoint until() const;
  };

 public:
  class Timer {
   public:
    explicit Timer(Uap::ptr uap);

    /**
     * \brief Get current interval offset.
     * \param time_offset Offset from current time is needed if you want to get
     * interval state in future.
     */
    Uap::IntervalState interval(Duration time_offset = {}) const;

    Uap::ptr uap_;
  };

  using SleepEvent = Event<void(Timer uap_timer)>;

  Uap(ObjProp prop, ObjPtr<Aether> aether,
      std::initializer_list<Interval> const& interval_list);

  AE_OBJECT_REFLECT(AE_MMBRS(aether_))

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_, aether_, current_interval_index_, intervals_);
  }

  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_, aether_, next_interval_index_, intervals_);
  }

  /**
   * \brief User notifies UAP - busyness logic is ready to sleep.
   */
  void SleepReady();

  /**
   * \brief Uap notifies user - all systems are ready to sleep up to sleep_until
   * time.
   */
  SleepEvent::Subscriber sleep_event();

  /**
   * \brief Set the intervals in order they should be applied \see Interval.
   */
  void SetIntervals(std::initializer_list<Interval> const& interval_list);

  /**
   * \brief Get timer.
   * Timer allows to get current active interval.
   */
  std::optional<Timer> timer();

 private:
  void GoToSleep();
  /**
   * \brief Get updated interval state \see Timer
   */
  IntervalState UpdateInterval(Duration time_offset);

  SleepEvent sleep_event_;

  ObjPtr<Aether> aether_;
  std::vector<Interval> intervals_;
  std::size_t current_interval_index_{};
  std::size_t next_interval_index_{};
  TimePoint start_time_;
};
}  // namespace ae

#endif  // AETHER_UAP_UAP_H_
