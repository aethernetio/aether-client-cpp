/*
 * Copyright 2024 Aethernet Inc.
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

#ifndef AETHER_EVENTS_EVENTS_MT_H_
#define AETHER_EVENTS_EVENTS_MT_H_

#include <mutex>

#include "aether/common.h"

namespace ae {
template <typename TMutex>
struct SharedMutexSyncPolicy {
  explicit SharedMutexSyncPolicy(TMutex& mutex) : mutex_{&mutex} {}

  template <typename TFunc>
  decltype(auto) InLock(TFunc&& func) {
    auto lock = std::lock_guard{*mutex_};
    return std::forward<TFunc>(func)();
  }

  TMutex* mutex_;
};

struct MutexSyncPolicy {
  template <typename TFunc>
  decltype(auto) InLock(TFunc&& func) {
    auto lock = std::lock_guard{mutex_};
    return std::forward<TFunc>(func)();
  }
  std::mutex mutex_;
};
}  // namespace ae

#endif  // AETHER_EVENTS_EVENTS_MT_H_
