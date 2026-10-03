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

#ifndef AETHER_EVENTS_EVENTS_H_
#define AETHER_EVENTS_EVENTS_H_

#include "aether/config.h"

// IWYU pragma: begin_exports
#include "aether/events/details/event_handler.h"
#include "aether/events/details/event_multi_subscription.h"
#include "aether/events/details/event_object.h"
#include "aether/events/details/event_subscription.h"
#include "aether/events/details/event_system.h"
#include "aether/events/details/generic_event_handler.h"
// IWYU pragma: end_exports

namespace ae {
/**
 * \brief Maximum number of simultaneously registered events.
 *
 * The default EventSystem has independent fixed capacities for event keys and
 * handlers. Event construction fails fast when this capacity is exhausted.
 */
static constexpr inline std::size_t kEventsCapacity =
    static_cast<std::size_t>(AE_EVENTS_MAX_COUNT);
/** \brief Maximum number of handlers in the default EventSystem pool. */
static constexpr inline std::size_t kEventsHandlerCapacity =
    static_cast<std::size_t>(AE_EVENTS_MAX_COUNT * 1.2);
/** \brief Maximum user-provided handler size in the default handler pool. */
static constexpr inline std::size_t kEventHandlerMaxSize =
    static_cast<std::size_t>(AE_EVENT_HANDLER_MAX_SIZE);
/** \brief Required alignment for user-provided handlers in the default pool. */
static constexpr inline std::size_t kEventHandlerAlign =
    static_cast<std::size_t>(AE_EVENT_HANDLER_ALIGN);

/** \brief The fixed-capacity event system used by the public event types. */
using EventSystem =
    events::EventSystem<kEventsCapacity, kEventsHandlerCapacity,
                        kEventHandlerMaxSize, kEventHandlerAlign>;

/**
 * \brief A multicast event backed by the default EventSystem.
 *
 * Emit() synchronously invokes a snapshot of eligible handlers. A by-value
 * argument must be copyable; each handler receives its own equivalent copy.
 * Reference arguments remain references during synchronous dispatch, so all
 * handlers observe the same object and must not retain its reference. Rvalue
 * reference arguments and move-only by-value arguments are unsupported.
 */
template <typename Signature>
using Event = events::EventObject<EventSystem, Signature>;

/**
 * \brief A lightweight, non-RAII handler registration.
 *
 * Discarding this token does not unsubscribe the handler. Transfer each token
 * returned by Subscribe() to exactly one Subscription or MultiSubscription to
 * give the registration an owner. RegEventHandler remains copyable as an
 * aggregate, but copies must not be retained by multiple owners; doing so is
 * unsupported misuse.
 */
using RegEventHandler = events::RegHandler<EventSystem>;

/** \brief RAII owner for one event registration. */
using Subscription = events::SubscriptionObject<EventSystem>;
/**
 * \brief RAII owner for several registrations using dynamic std::vector
 * storage.
 *
 * This compatibility type can allocate while growing. Use
 * events::MultiSubscriptionObjectFix when fixed-capacity storage is required.
 */
using MultiSubscription = events::MultiSubscriptionObjectDyn<EventSystem>;

using events::EventContext;

}  // namespace ae

#endif  // AETHER_EVENTS_EVENTS_H_
