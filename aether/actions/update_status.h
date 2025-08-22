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

#ifndef AETHER_ACTIONS_UPDATE_STATUS_H_
#define AETHER_ACTIONS_UPDATE_STATUS_H_

#include <cstdint>
#include <algorithm>

#include "aether/common.h"

namespace ae {
enum class UpdateStatusType : std::uint8_t {
  kNothing,
  kDelay,
  kResult,
  kError,
  kStop,
};

/**
 * \brief Action update status.
 * Action must return update status on Update function.
 */
struct UpdateStatus {
  static UpdateStatus Result() {
    return UpdateStatus{UpdateStatusType::kResult};
  }
  static UpdateStatus Error() { return UpdateStatus{UpdateStatusType::kError}; }
  static UpdateStatus Stop() { return UpdateStatus{UpdateStatusType::kStop}; }
  static UpdateStatus Delay(TimePoint delay_to) {
    return UpdateStatus{UpdateStatusType::kDelay, delay_to};
  }

  template <typename... U>
  static UpdateStatus Merge(U&&... u) {
    static_assert((std::is_same_v<UpdateStatus, std::decay_t<U>> && ...),
                  "Merge only UpdateStatuses");

    if (((u.type == UpdateStatusType::kError) || ...)) {
      return Error();
    }
    if (((u.type == UpdateStatusType::kStop) || ...)) {
      return Stop();
    }
    if (((u.type == UpdateStatusType::kResult) || ...)) {
      return Result();
    }
    // if some contains Delay, count the min delay and return Delay
    if (((u.type == UpdateStatusType::kDelay) || ...)) {
      TimePoint tp = TimePoint::clock::now() + std::chrono::hours(8086);
      (
          [&](auto const& res) {
            if (res.type == UpdateStatusType::kDelay) {
              tp = std::min(res.delay_to, tp);
            }
          }(std::forward<U>(u)),
          ...);
      return UpdateStatus{UpdateStatusType::kDelay, tp};
    }
    return {};
  }

  constexpr UpdateStatus() : type{UpdateStatusType::kNothing} {}
  constexpr explicit UpdateStatus(UpdateStatusType t) : type{t} {}
  constexpr explicit UpdateStatus(UpdateStatusType t, TimePoint d)
      : type{t}, delay_to{d} {}

  UpdateStatusType type;
  TimePoint delay_to;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_UPDATE_STATUS_H_
