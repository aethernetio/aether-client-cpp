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

#ifndef AETHER_PLATFORM_EMSCRIPTEN_MAIN_LOOP_H_
#define AETHER_PLATFORM_EMSCRIPTEN_MAIN_LOOP_H_

#if defined(__EMSCRIPTEN__)

#  include <chrono>
#  include <functional>
#  include <utility>

#  include <emscripten.h>

#  include "aether/aether_app.h"
#  include "aether/clock.h"

namespace ae {
namespace emscripten_main_loop_internal {

struct LoopState {
  AetherApp* app{nullptr};
  std::function<void()> tick_hook;
  std::chrono::milliseconds max_slice{50};
};

inline void ScheduleTick(void* user_data) {
  auto* st = static_cast<LoopState*>(user_data);
  if (st->app->IsExited()) {
    delete st;
    return;
  }

  auto const now = Now();
  auto const next = st->app->Update(now);
  if (st->tick_hook) {
    st->tick_hook();
  }

  auto delay_ms = 0;
  if (next != TimePoint::max() && next > now) {
    auto delay =
        std::chrono::duration_cast<std::chrono::milliseconds>(next - now);
    if (delay > st->max_slice) {
      delay = st->max_slice;
    }
    delay_ms = static_cast<int>(delay.count());
  } else if (next == TimePoint::max()) {
    delay_ms = static_cast<int>(st->max_slice.count());
  }

  emscripten_async_call(&ScheduleTick, st, delay_ms);
}

}  // namespace emscripten_main_loop_internal

/**
 * \brief Cooperative browser event loop for AetherApp.
 *
 * Do not call blocking WaitUntil / WaitActions on the browser main thread.
 * Browser code must drive cooperative Update scheduling:
 *   next = app.Update(now)
 *   delay = clamp(next - now, 0, max_slice)
 *   setTimeout / emscripten_async_call(schedule, delay)
 *
 * \param app Aether application (must outlive the loop until Exit()).
 * \param tick_hook Optional per-iteration hook (UI poll, IDBFS flush, …).
 * \param max_slice Upper bound on delay between updates (default 50ms).
 */
inline void RunAetherBrowserLoop(
    AetherApp& app, std::function<void()> tick_hook = {},
    std::chrono::milliseconds max_slice = std::chrono::milliseconds{50}) {
  auto* state = new emscripten_main_loop_internal::LoopState{
      &app, std::move(tick_hook), max_slice};
  emscripten_main_loop_internal::ScheduleTick(state);
}

}  // namespace ae

#endif  // defined(__EMSCRIPTEN__)
#endif  // AETHER_PLATFORM_EMSCRIPTEN_MAIN_LOOP_H_
