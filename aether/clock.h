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

#ifndef AETHER_CLOCK_H_
#define AETHER_CLOCK_H_

#include <chrono>
#include <cstdint>

#include "aether/env.h"

namespace ae::clock_internal {
using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::system_clock;

template <typename ChronoClock>
class SyncClock {
 public:
  static RTC_STORAGE_ATTR system_clock::duration SyncTimeDiff;

  using internal_clock = ChronoClock;
  using rep = typename ChronoClock::rep;
  using period = typename ChronoClock::period;
  using duration = typename ChronoClock::duration;

  using time_point = std::chrono::time_point<SyncClock, duration>;

  static constexpr bool is_steady = ChronoClock::is_steady;

  /**
   * \brief Get the current time with SyncTimeDiff
   */
  static time_point now() {
    return time_point{ChronoClock::now().time_since_epoch() + SyncTimeDiff};
  }

  /**
   * \brief Return sync time
   */
  template <typename C, typename D>
  static std::enable_if_t<std::is_same_v<SyncClock, C> ||
                              std::is_same_v<internal_clock, C>,
                          time_point>
  sync_time(std::chrono::time_point<C, D> const& tp) {
    return time_point{tp.time_since_epoch() + SyncTimeDiff};
  }
};

template <typename ChronoClock>
std::chrono::system_clock::duration SyncClock<ChronoClock>::SyncTimeDiff =
    std::chrono::milliseconds{0};
}  // namespace ae::clock_internal

namespace ae {
using std::chrono_literals::operator""h;
using std::chrono_literals::operator""min;
using std::chrono_literals::operator""s;
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""us;
using std::chrono_literals::operator""ns;
/**
 * \brief Represents microseconds in uint32_t.
 */
using Duration = std::chrono::duration<std::uint32_t, std::micro>;
using SystemClock = std::chrono::system_clock;
using SyncClock = clock_internal::SyncClock<SystemClock>;
using TimePoint = typename SystemClock::time_point;
using SyncTimePoint = typename SyncClock::time_point;

// current system clock time, without synchorinization
inline auto Now() { return SystemClock::now(); }
// synchronised time
inline auto SyncTime() { return SyncClock::now(); }
}  // namespace ae

#endif  // AETHER_CLOCK_H_
