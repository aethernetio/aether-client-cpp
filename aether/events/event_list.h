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
#include <atomic>
#include <cstddef>
#include <algorithm>

#include "aether/events/events_mt.h"
#include "aether/events/event_handler.h"
#include "aether/types/aligned_storage.h"

namespace ae {
template <typename TSyncPolicy = NoLockSyncPolicy>
class EventHandlersList : public TSyncPolicy {
  using TemplateHandler = EventHandler<void(int)>;
  // all event handlers has the same size
  static constexpr std::size_t kElementSize = sizeof(TemplateHandler);
  static constexpr std::size_t kElementAlign = alignof(TemplateHandler);
  using StoreType = ManagedStorage<kElementSize, kElementAlign>;

 public:
  /**
   * \brief The iterator adapter provide interface for 'for range loop' and lock
   * list from actual removing any elements.
   * Removed elements will be deleted if no other IteratorAdapter to that list
   * exists
   */
  template <typename TSignature>
  class IteratorAdapter {
   public:
    /**
     * \brief Actual Iterator to iterate through the list of event handlers.
     * Return only is_alive handlers.
     */
    class Iterator {
     public:
      Iterator() = default;

      Iterator(typename std::list<StoreType>::iterator it, std::uint16_t index,
               std::uint16_t size)
          : size_{size} {
        auto counter = index;
        for (auto i = it; counter < size_; ++i, ++counter) {
          if (i->ptr<EventHandler<TSignature>>()->is_alive()) {
            it_ = i;
            index_ = counter;
            return;
          }
        }
        index_ = size_;
      }

      AE_CLASS_COPY_MOVE(Iterator)

      Iterator& operator++() {
        it_++;
        index_++;
        if (index_ == size_) {
          return *this;
        }
        if (!it_->ptr<EventHandler<TSignature>>()->is_alive()) {
          return operator++();
        }
        return *this;
      }

      Iterator operator++(int) {
        Iterator tmp(*this);
        ++(*this);
        return tmp;
      }

      bool operator==(const Iterator& other) const {
        return index_ == other.index_;
      }

      bool operator!=(const Iterator& other) const { return !(*this == other); }

      EventHandler<TSignature>& operator*() const {
        return *it_->ptr<EventHandler<TSignature>>();
      }

      EventHandler<TSignature>* operator->() const {
        return it_->ptr<EventHandler<TSignature>>();
      }

     private:
      typename std::list<StoreType>::iterator it_;
      /**
       * index and size used to control the end of the list.
       * using end iterator is not appropriate here because it's grow with new
       * elements added.
       */
      std::uint16_t index_;
      std::uint16_t size_;
    };

    using iterator = Iterator;

    explicit IteratorAdapter(EventHandlersList* self)
        : self_{self},
          begin_{std::begin(self_->handlers_), 0,
                 static_cast<std::uint16_t>(self_->handlers_.size())},
          end_{std::end(self_->handlers_),
               static_cast<std::uint16_t>(self_->handlers_.size()),
               static_cast<std::uint16_t>(self_->handlers_.size())} {
      ++self_->use_counter_;
    }

    ~IteratorAdapter() {
      if ((--self_->use_counter_) == 0) {
        self_->CleanUp();
      }
    }

    AE_CLASS_COPY_MOVE(IteratorAdapter)

    iterator begin() { return begin_; }
    iterator end() { return end_; }

   private:
    EventHandlersList* self_;
    iterator begin_;
    iterator end_;
  };

  explicit EventHandlersList(TSyncPolicy&& policy)
      : TSyncPolicy(std::move(policy)) {}

  /**
   * \brief Insert a new event handler into the list.
   * The TSignature must be always be the same. We can't control it on this
   * level, so it's user's responsibility to ensure it.
   */
  template <typename TSignature>
  std::size_t Insert(EventHandler<TSignature>&& handler) {
    return TSyncPolicy::InLock([&]() {
      handlers_.emplace_back(std::move(handler));
      return handlers_.size() - 1;
    });
  }

  /**
   * \brief Get access to iterator on handlers.
   * The TSignature must be always be the same. We can't control it on this
   * level, so it's user's responsibility to ensure it.
   */
  template <typename TSignature>
  IteratorAdapter<TSignature> Iterator() {
    return TSyncPolicy::InLock(
        [&]() { return IteratorAdapter<TSignature>{this}; });
  }

  /**
   * \brief Remove element by index.
   * It only marks the handler as dead.
   */
  void Remove(std::size_t index) {
    TSyncPolicy::InLock([&]() {
      if (index <= handlers_.size()) {
        std::next(std::begin(handlers_), static_cast<std::ptrdiff_t>(index))
            ->template ptr<IEventHandler>()
            ->set_alive(false);
      }
    });
  }

  /**
   * \brief Check if element is alive by index.
   */
  bool Alive(std::size_t index) const {
    return TSyncPolicy::InLock([&]() {
      if (index <= handlers_.size()) {
        return std::next(std::begin(handlers_),
                         static_cast<std::ptrdiff_t>(index))
            -> template ptr<IEventHandler>()
            ->is_alive();
      }
      return false;
    });
  }

 private:
  /**
   * \brief Remove last unalived handlers.
   */
  void CleanUp() {
    TSyncPolicy::InLock([&]() {
      if (use_counter_ != 0) {
        return;
      }
      // remove only from the end
      auto remove_begin = std::end(handlers_);
      auto it = std::find_if(
          std::rbegin(handlers_), std::rend(handlers_), [](auto const& h) {
            return h.template ptr<IEventHandler>()->is_alive();
          });
      if (it != std::rend(handlers_)) {
        remove_begin = it.base();
      }
      handlers_.erase(remove_begin, std::end(handlers_));
    });
  }

  std::atomic_int16_t use_counter_{0};
  std::list<StoreType> handlers_;
};
}  // namespace ae

#endif  // AETHER_EVENTS_EVENT_LIST_H_
