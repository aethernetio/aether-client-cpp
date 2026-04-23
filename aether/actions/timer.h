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

#ifndef AETHER_ACTIONS_TIMER_H_
#define AETHER_ACTIONS_TIMER_H_

#include <utility>
#include <optional>
#include <type_traits>

#include "aether/clock.h"
#include "aether/ae_context.h"
#include "aether/actions/action2_.h"
#include "aether/types/small_function.h"

namespace ae {
class Timer final : public a2::Action {
 public:
  using TimerFunc = SmallFunction<void()>;

  template <typename F, typename Time = TimePoint>
    requires(std::is_same_v<Time, TimePoint> || std::is_same_v<Time, Duration>)
  Timer(AeContext const& ae_context, F&& func, Time timeout)
      : ae_context_{ae_context},
        alive_ctx_{std::in_place, ae_context_, this},
        func_{std::forward<F>(func)} {
    // schedule timer
    ae_context_.scheduler().DelayedTask(
        [imalive{alive_ctx_->View()}]() {
          if (imalive) {
            imalive->Invoke();
          }
        },
        timeout);
  }

  void Reset() { alive_ctx_.reset(); }

 private:
  void Invoke() {
    func_();
    Finish();
  }

  AeContext ae_context_;
  std::optional<IndexCtx<Timer>> alive_ctx_;
  TimerFunc func_;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_TIMER_H_
