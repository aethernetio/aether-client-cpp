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

#ifndef AETHER_ACTIONS_ACTION_RESULT_H_
#define AETHER_ACTIONS_ACTION_RESULT_H_

#include <cstdint>
#include <algorithm>

#include "aether/common.h"

namespace ae {
enum class ResultType : std::uint8_t {
  kNothing,
  kDelay,
  kResult,
  kError,
  kStop,
};

/**
 * \brief Action result.
 * Action must return action result on Update function.
 */
struct ActionResult {
  static ActionResult Result() { return ActionResult{ResultType::kResult}; }
  static ActionResult Error() { return ActionResult{ResultType::kError}; }
  static ActionResult Stop() { return ActionResult{ResultType::kStop}; }
  static ActionResult Delay(TimePoint delay_to) {
    return ActionResult{ResultType::kDelay, delay_to};
  }

  template <typename... U>
  static ActionResult Merge(U&&... u) {
    static_assert((std::is_same_v<ActionResult, std::decay_t<U>> && ...),
                  "Merge only ActionResults");

    if (((u.type == ResultType::kError) || ...)) {
      return Error();
    }
    if (((u.type == ResultType::kStop) || ...)) {
      return Stop();
    }
    if (((u.type == ResultType::kResult) || ...)) {
      return Result();
    }
    // if some contains Delay, count the min delay and return Delay
    if (((u.type == ResultType::kDelay) || ...)) {
      TimePoint tp = TimePoint::clock::now() + std::chrono::hours(8086);
      (
          [&](auto const& res) {
            if (res.type == ResultType::kDelay) {
              tp = std::min(res.delay_to, tp);
            }
          }(std::forward<U>(u)),
          ...);
      return ActionResult{ResultType::kDelay, tp};
    }
    return {};
  }

  ActionResult() : type{ResultType::kNothing} {}
  explicit ActionResult(ResultType t) : type{t} {}
  explicit ActionResult(ResultType t, TimePoint d) : type{t}, delay_to{d} {}

  ResultType type;
  TimePoint delay_to;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_ACTION_RESULT_H_
