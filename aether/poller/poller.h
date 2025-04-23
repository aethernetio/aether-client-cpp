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

#ifndef AETHER_POLLER_POLLER_H_
#define AETHER_POLLER_POLLER_H_

#include "aether/obj/obj.h"
#include "aether/events/events.h"
#include "aether/events/events_mt.h"
#include "aether/poller/poller_types.h"

namespace ae {
class IPoller : public Obj {
  AE_OBJECT(IPoller, Obj, 0)

 protected:
  IPoller() = default;

 public:
  /**
   * \brief Event type for event.
   * User should check event.descriptor to match with its own.
   */
  using OnPollEvent = Event<void(PollerEvent event),
                            SharedMutexSyncPolicy<std::recursive_mutex>>;
  using OnPollEventSubscriber = typename OnPollEvent::Subscriber;

#if defined AE_DISTILLATION
  explicit IPoller(Domain* domain);
#endif
  ~IPoller() override;

  AE_OBJECT_REFLECT()

  /**
   * \brief Add event for descriptor
   * User must subscribe to returned event subscriber, \see OnPollEvent
   */
  [[nodiscard]] virtual OnPollEventSubscriber Add(DescriptorType descriptor);
  /**
   * \brief Remove event for descriptor
   */
  virtual void Remove(DescriptorType descriptor);
};
}  // namespace ae

#endif  // AETHER_POLLER_POLLER_H_ */
