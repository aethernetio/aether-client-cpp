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

#ifndef AETHER_EVENTS_DETAILS_EVENTS_MAP_H_
#define AETHER_EVENTS_DETAILS_EVENTS_MAP_H_

#include <concepts>
#include <cstddef>
#include <utility>

#include <etl/flat_multimap.h>
#include <etl/generic_pool.h>

#include "aether/events/details/event_handler.h"
#include "aether/events/details/event_key_registry.h"

namespace ae::events {
/**
 * \brief Owns fixed-capacity storage and the key-to-handler relationships.
 */
template <std::size_t Capacity, std::size_t ElemSize, std::size_t ElemAlign>
class EventHandlerMap {
 public:
  using Key = ::ae::events::Key;
  using Type = IHandler*;
  using Map = etl::flat_multimap<Key, Type, Capacity>;
  using Iterator = typename Map::iterator;
  using Pool = etl::generic_pool<ElemSize, ElemAlign, Capacity>;

  EventHandlerMap() = default;
  ~EventHandlerMap();

  template <std::derived_from<IHandler> Handler>
  [[nodiscard]] auto Add(Key key, Handler&& handler) -> std::decay_t<Handler>*;
  // Deactivate one exact key-to-handler relationship. A referenced handler is
  // retained for outstanding snapshots; otherwise its storage is reclaimed.
  void Deactivate(Key key, IHandler const* handler) noexcept;
  // Reclaim unreferenced handlers for key. This intentionally leaves active
  // state unchanged for retained snapshot handlers.
  void Prune(Key key) noexcept;
  // Reclaim deactivated, unreferenced handlers for a still-registered key.
  void PruneDeactivated(Key key) noexcept;
  // Get the [begin, end) range of elements with key.
  std::pair<Iterator, Iterator> Get(Key key);

 private:
  Iterator Destroy(Iterator iterator) noexcept;

  Pool pool_;
  Map events_map_;
};

template <std::size_t Capacity, std::size_t ElemSize, std::size_t ElemAlign>
EventHandlerMap<Capacity, ElemSize, ElemAlign>::~EventHandlerMap() {
  while (!events_map_.empty()) {
    Destroy(events_map_.begin());
  }
}

template <std::size_t Capacity, std::size_t ElemSize, std::size_t ElemAlign>
template <std::derived_from<IHandler> Handler>
auto EventHandlerMap<Capacity, ElemSize, ElemAlign>::Add(Key key,
                                                         Handler&& handler)
    -> std::decay_t<Handler>* {
  using RawType = std::decay_t<Handler>;
  static_assert(sizeof(RawType) <= ElemSize,
                "An event handler must fit into map storage");
  static_assert(alignof(RawType) <= ElemAlign,
                "An event handler must fit into map storage alignment");

  if (events_map_.full() || pool_.full()) {
    return nullptr;
  }
  auto* element =
      pool_.template create<RawType>(std::forward<Handler>(handler));
  if (element == nullptr) {
    return nullptr;
  }
  auto [_, inserted] = events_map_.emplace(key, element);
  if (!inserted) {
    pool_.destroy(static_cast<IHandler*>(element));
    return nullptr;
  }
  return element;
}

template <std::size_t Capacity, std::size_t ElemSize, std::size_t ElemAlign>
void EventHandlerMap<Capacity, ElemSize, ElemAlign>::Deactivate(
    Key key, IHandler const* handler) noexcept {
  auto [begin, end] = Get(key);
  for (auto iterator = begin; iterator != end; ++iterator) {
    if (iterator->second == handler) {
      iterator->second->active = false;
      if (iterator->second->ref_cntr == 0) {
        Destroy(iterator);
      }
      return;
    }
  }
}

template <std::size_t Capacity, std::size_t ElemSize, std::size_t ElemAlign>
void EventHandlerMap<Capacity, ElemSize, ElemAlign>::Prune(Key key) noexcept {
  auto iterator = Get(key).first;
  while (iterator != events_map_.end() && iterator->first == key) {
    if (iterator->second->ref_cntr == 0) {
      iterator = Destroy(iterator);
    } else {
      ++iterator;
    }
  }
}

template <std::size_t Capacity, std::size_t ElemSize, std::size_t ElemAlign>
void EventHandlerMap<Capacity, ElemSize, ElemAlign>::PruneDeactivated(
    Key key) noexcept {
  auto iterator = Get(key).first;
  while (iterator != events_map_.end() && iterator->first == key) {
    auto const* handler = iterator->second;
    if (!handler->active && handler->ref_cntr == 0) {
      iterator = Destroy(iterator);
    } else {
      ++iterator;
    }
  }
}

template <std::size_t Capacity, std::size_t ElemSize, std::size_t ElemAlign>
auto EventHandlerMap<Capacity, ElemSize, ElemAlign>::Get(Key key)
    -> std::pair<Iterator, Iterator> {
  return events_map_.equal_range(key);
}

template <std::size_t Capacity, std::size_t ElemSize, std::size_t ElemAlign>
auto EventHandlerMap<Capacity, ElemSize, ElemAlign>::Destroy(
    Iterator iterator) noexcept -> Iterator {
  pool_.destroy(iterator->second);
  return events_map_.erase(iterator);
}

}  // namespace ae::events

#endif  // AETHER_EVENTS_DETAILS_EVENTS_MAP_H_
