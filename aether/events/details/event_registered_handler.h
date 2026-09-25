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

#ifndef AETHER_EVENTS_DETAILS_EVENT_REGISTERED_HANDLER_H_
#define AETHER_EVENTS_DETAILS_EVENT_REGISTERED_HANDLER_H_

#include "aether/events/details/event_handler.h"
#include "aether/events/details/event_system.h"

namespace ae::events {
/**
 * \brief Registered event handler.
 * Stores an event key, a handler pointer, and an event-system pointer.
 *
 * This aggregate is a lightweight, non-owning registration token. A token
 * returned by EventObject::Subscribe is valid and has non-null handler and
 * event-system pointers. Destroying it does not unsubscribe the handler; use
 * SubscriptionObject or MultiSubscriptionObject to manage registration
 * lifetime.
 *
 * RegHandler remains copyable as an aggregate, but that copyability does not
 * permit shared ownership. Transfer every returned token to exactly one
 * SubscriptionObject or MultiSubscriptionObject. Copying or retaining a token
 * for multiple such owners is unsupported misuse.
 */
template <EventSystemConcept Es>
struct RegHandler {
  Key key;
  IHandler* h;
  Es* es;
};
}  // namespace ae::events

#endif  // AETHER_EVENTS_DETAILS_EVENT_REGISTERED_HANDLER_H_
