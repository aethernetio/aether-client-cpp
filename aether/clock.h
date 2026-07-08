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
using TimePoint = typename SystemClock::time_point;

// current system clock time, without synchorinization
inline auto Now() { return SystemClock::now(); }

}  // namespace ae

#endif  // AETHER_CLOCK_H_
