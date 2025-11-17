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

#ifndef AETHER_EVENTS_EVENT_DELETER_H_
#define AETHER_EVENTS_EVENT_DELETER_H_

#include "aether/common.h"
#include "aether/ptr/rc_ptr.h"
#include "aether/events/event_list.h"

namespace ae {
/**
 * \brief Class for remove event handlers that are not needed anymore.
 * \see Subscription for RAII wrapper.
 */
class EventHandlerDeleter {
 public:
  EventHandlerDeleter(RcPtr<EventHandlersList> const& event_handlers,
                      typename EventHandlersList::Index index);
  AE_CLASS_COPY_MOVE(EventHandlerDeleter)

  void Delete();

  bool alive() const;

 private:
  RcPtrView<EventHandlersList> event_handlers_;
  typename EventHandlersList::Index index_;
};

}  // namespace ae

#endif  // AETHER_EVENTS_EVENT_DELETER_H_
