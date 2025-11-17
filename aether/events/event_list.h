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

#ifndef AETHER_EVENTS_EVENT_LIST_H_
#define AETHER_EVENTS_EVENT_LIST_H_

#include <list>
#include <mutex>
#include <atomic>
#include <cstddef>
#include <algorithm>

#include "aether/events/event_handler.h"
#include "aether/types/aligned_storage.h"

namespace ae {
class EventHandlersList {
  using TemplateHandler = EventHandler<void(int)>;
  // all event handlers has the same size
  static constexpr std::size_t kElementSize = sizeof(TemplateHandler);
  static constexpr std::size_t kElementAlign = alignof(TemplateHandler);
  using ValueType = ManagedStorage<kElementSize, kElementAlign>;
  struct StoreType {
    template <typename U,
              AE_REQUIRERS_NOT((std::is_same<StoreType, std::decay_t<U>>))>
    explicit StoreType(U&& value) : value{std::forward<U>(value)} {}

    ValueType value;
    bool is_alive = true;
  };

  using ListType = std::list<StoreType>;

 public:
  struct Index {
    typename ListType::iterator it;
  };

  /**
   * \brief The iterator adapter provide interface for 'for range loop' and lock
   * list from actual removing any elements.
   * Removed elements will be deleted if no other IteratorAdapter to that list
   * exists
   */
  class IteratorAdapter {
   public:
    /**
     * \brief Actual Iterator to iterate through the list of event handlers.
     * Return only is_alive handlers.
     */
    class Iterator {
     public:
      Iterator() = default;

      Iterator(typename ListType::iterator it, std::uint16_t index,
               std::uint16_t size);

      AE_CLASS_COPY_MOVE(Iterator)

      Iterator& operator++();

      Iterator operator++(int);

      bool operator==(const Iterator& other) const;

      bool operator!=(const Iterator& other) const;
      template <typename TSignature>
      auto* get() const {
        return it_->value.template ptr<EventHandler<TSignature>>();
      }

     private:
      typename ListType::iterator it_;
      /**
       * index and size used to control the end of the list.
       * using end iterator is not appropriate here because it's grow with new
       * elements added.
       */
      std::uint16_t index_;
      std::uint16_t size_;
    };

    using iterator = Iterator;

    explicit IteratorAdapter(EventHandlersList* self);
    ~IteratorAdapter();

    AE_CLASS_COPY_MOVE(IteratorAdapter)

    iterator begin() { return begin_; }
    iterator end() { return end_; }

   private:
    EventHandlersList* self_;
    iterator begin_;
    iterator end_;
  };

  EventHandlersList() = default;

  /**
   * \brief Insert a new event handler into the list.
   * The TSignature must be always be the same. We can't control it on this
   * level, so it's user's responsibility to ensure it.
   */
  template <typename TSignature>
  Index Insert(EventHandler<TSignature>&& handler) {
    auto lock = std::scoped_lock{lock_handlers_};
    auto it = handlers_.emplace(handlers_.end(), std::move(handler));
    return Index{it};
  }

  /**
   * \brief Get access to iterator on handlers.
   * The TSignature must be always be the same. We can't control it on this
   * level, so it's user's responsibility to ensure it.
   */
  IteratorAdapter Iterator();

  /**
   * \brief Remove element by index.
   * It only marks the handler as dead.
   */
  void Remove(Index index);

  /**
   * \brief Check if element is alive by index.
   */
  bool Alive(Index index) const;

 private:
  /**
   * \brief Remove last unalived handlers.
   */
  void CleanUp();

  std::atomic_int16_t use_counter_{0};
  mutable std::mutex lock_handlers_;
  ListType handlers_;
};
}  // namespace ae

#endif  // AETHER_EVENTS_EVENT_LIST_H_
