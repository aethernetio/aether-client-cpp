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

#ifndef AETHER_EVENTS_DETAILS_EVENT_SYSTEM_H_
#define AETHER_EVENTS_DETAILS_EVENT_SYSTEM_H_

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>

#include <etl/vector.h>

#include "aether/events/details/event_handler.h"
#include "aether/events/details/event_handler_map.h"
#include "aether/events/details/event_key_registry.h"

namespace ae::events {
template <typename T>
concept EventSystemConcept = requires(T& t) {
  // for event system required
  // be able to reg event
  { t.RegEvent() } -> std::same_as<std::optional<Key>>;
  // be able to unreg event
  { t.UnregEvent(Key{0}) };
  // be able to add handler
  { t.AddHandler(Key{0}, std::declval<Handler<void(int, bool)>&&>()) };
  // be able to remove handlers by the stored handler pointer
  { t.RemoveHandler(Key{}, std::declval<IHandler const*>()) };
  // be able to invoke event with added handlers
  { t.Invoke(Key{0}, 1, true) };
};

/**
 * \brief Thread-safe, fixed-capacity central event management system.
 *
 * An event owns one registered key and handlers are allocated from this
 * system's fixed pool. Invoke() snapshots handlers while holding the mutex,
 * then invokes them synchronously without the mutex. Consequently callbacks
 * may subscribe, unsubscribe, emit recursively, and register events.
 *
 * RegEvent() returns std::nullopt and AddHandler() returns nullptr when their
 * respective capacities are exhausted. A handler must fit within kElemSize and
 * kElemAlign; the effective element size includes the IHandler base object.
 * EventObject terminates on these resource failures.
 *
 * Unsubscribing makes a handler ineligible for later snapshot checks but does
 * not wait for a callback already in a snapshot; a handler removed after its
 * eligibility check may still run. Unregistering an event rejects new
 * invocations and registrations, but does not deactivate handlers already
 * captured by an active invocation snapshot. Those handlers continue unless
 * individually unsubscribed. Handler references keep snapshotted handlers
 * alive during recursive dispatch.
 */
template <std::size_t EventCapacity, std::size_t HandlerCapacity,
          std::size_t MaxElemSize, std::size_t MaxElemAlign>
class EventSystem {
 public:
  // must fit at least an IHandler
  static constexpr std::size_t kElemSize = MaxElemSize + sizeof(IHandler);
  // must align at least as IHandler
  static constexpr std::size_t kElemAlign =
      std::max(MaxElemAlign, alignof(IHandler));
  using Registry = KeyRegistry<EventCapacity>;
  using Map = EventHandlerMap<HandlerCapacity, kElemSize, kElemAlign>;
  using Key = ::ae::events::Key;

  // Registers a new event and returns its key, or std::nullopt on exhaustion.
  [[nodiscard]] std::optional<Key> RegEvent();
  // Unregisters an event. Active invocation snapshots still run their handlers.
  void UnregEvent(Key key);
  // check if event registered
  bool IsRegistered(Key key) const;
  // Adds a handler for a registered key, or returns nullptr on exhaustion.
  template <std::derived_from<IHandler> Handler>
  [[nodiscard]] auto AddHandler(Key key, Handler&& handler)
      -> std::decay_t<Handler>*;
  // Remove a handler by its non-owning registration pointer. This form lets
  // RAII subscriptions avoid dereferencing a token whose handler is gone,
  // including one retained by a retiring event snapshot.
  void RemoveHandler(Key key, IHandler const* handler);
  // Invoke the handler for event under a key
  template <typename... Args>
  void Invoke(Key key, Args... args);

 private:
  void ReleaseRetiringKey(Key key);

  mutable std::mutex mutex_;
  Registry key_registry_;
  Map event_handlers_map_;
};

template <std::size_t EC, std::size_t HC, std::size_t S, std::size_t A>
auto EventSystem<EC, HC, S, A>::RegEvent() -> std::optional<Key> {
  auto lock = std::scoped_lock{mutex_};
  return key_registry_.Register();
}

template <std::size_t EC, std::size_t HC, std::size_t S, std::size_t A>
void EventSystem<EC, HC, S, A>::UnregEvent(Key key) {
  auto lock = std::scoped_lock{mutex_};
  if (!key_registry_.IsRegistered(key)) {
    return;
  }
  key_registry_.Retire(key);
  event_handlers_map_.Prune(key);
  ReleaseRetiringKey(key);
}

template <std::size_t EC, std::size_t HC, std::size_t S, std::size_t A>
bool EventSystem<EC, HC, S, A>::IsRegistered(Key key) const {
  auto lock = std::scoped_lock{mutex_};
  return key_registry_.IsRegistered(key);
}

template <std::size_t EC, std::size_t HC, std::size_t S, std::size_t A>
template <std::derived_from<IHandler> T>
auto EventSystem<EC, HC, S, A>::AddHandler(Key key, T&& handler)
    -> std::decay_t<T>* {
  auto lock = std::scoped_lock{mutex_};
  if (!key_registry_.IsRegistered(key)) {
    return nullptr;
  }

  return event_handlers_map_.Add(key, std::forward<T>(handler));
}

template <std::size_t EC, std::size_t HC, std::size_t S, std::size_t A>
void EventSystem<EC, HC, S, A>::RemoveHandler(Key key,
                                              IHandler const* handler) {
  auto lock = std::scoped_lock{mutex_};
  event_handlers_map_.Deactivate(key, handler);
}

template <std::size_t EC, std::size_t HC, std::size_t S, std::size_t A>
template <typename... Args>
void EventSystem<EC, HC, S, A>::Invoke(Key key, Args... args) {
  // all handlers must be derived from Handler<Signature>
  using HandlerType = Handler<void(Args...)>;
  // first collect handlers in one vector
  // increase ref counters, so somebody will not destroy them
  etl::vector<HandlerType*, HC> handlers;
  {
    auto lock = std::scoped_lock{mutex_};
    if (!key_registry_.IsRegistered(key)) {
      return;
    }
    auto [begin, end] = event_handlers_map_.Get(key);
    for (auto i = begin; i != end; ++i) {
      if (i->second->active) {
        i->second->ref_cntr++;
        handlers.emplace_back(static_cast<HandlerType*>(i->second));
      }
    }
  }

  // this could be unpredictable long operation
  // so invoke handlers without mutex
  // this allow to add new events and subscribe new handlers from the invoked
  // handler or from other threads
  for (auto* h : handlers) {
    // handlers could be removed during invoke of previous one
    // Check eligibility immediately before invoking. A handler removed after
    // this check may still run; unsubscription never waits for callbacks.
    if (h->active) {
      // args are named lvalues here. By-value signature arguments are copied
      // for every handler rather than moved from by an earlier callback.
      h->Invoke(args...);
    }
  }

  // Decrease used ref counters and cleanup orphan handlers
  // by orphan we meant either this key was unregistered
  // or handler become inactive
  {
    auto lock = std::scoped_lock{mutex_};
    for (auto* h : handlers) {
      h->ref_cntr--;
    }
    if (key_registry_.IsRetiring(key)) {
      event_handlers_map_.Prune(key);
      ReleaseRetiringKey(key);
    } else if (key_registry_.IsRegistered(key)) {
      event_handlers_map_.PruneDeactivated(key);
    }
  }
}

template <std::size_t EC, std::size_t HC, std::size_t S, std::size_t A>
void EventSystem<EC, HC, S, A>::ReleaseRetiringKey(Key key) {
  auto const [begin, end] = event_handlers_map_.Get(key);
  if (key_registry_.IsRetiring(key) && begin == end) {
    key_registry_.Release(key);
  }
}

}  // namespace ae::events

#endif  // AETHER_EVENTS_DETAILS_EVENT_SYSTEM_H_
