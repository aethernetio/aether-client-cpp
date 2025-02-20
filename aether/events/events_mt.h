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
#include "aether/events/events.h"

namespace ae {
/**
 * \brief Helper class to make event subscriptions in multithread context.
 * \see EventSubscriber
 */
template <typename TSignature, typename TMutex>
class MtEventSubscriber final : EventSubscriber<TSignature> {
 public:
  explicit MtEventSubscriber(Event<TSignature>& event,
                             std::unique_lock<TMutex> lock)
      : EventSubscriber<TSignature>{event}, lock_{std::move(lock)} {}

  AE_CLASS_MOVE_ONLY(MtEventSubscriber)

  using EventSubscriber<TSignature>::Subscribe;

 private:
  std::unique_lock<TMutex> lock_;
};

}  // namespace ae

#endif  // AETHER_EVENTS_EVENTS_MT_H_
