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

#include <utility>

#include "aether/common.h"
#include "aether/ptr/storage.h"
#include "aether/events/event_impl.h"

namespace ae {
namespace events_internal {
class EventHandlerDeleterImplBase {
 public:
  // Doesn't required
  // virtual ~EventHandlerDeleterImplBase() = default;

  virtual void Delete() = 0;
  virtual bool alive() const = 0;
};

template <typename Signature, typename TSyncPolicy>
class EventHandlerDeleterImpl final : public EventHandlerDeleterImplBase {
 public:
  EventHandlerDeleterImpl(
      RcPtr<EventImpl<Signature, TSyncPolicy>> const& event_impl,
      std::size_t index)
      : event_impl_{event_impl}, index_{index} {}

  AE_CLASS_COPY_MOVE(EventHandlerDeleterImpl);

  void Delete() override {
    if (auto ptr = event_impl_.lock(); ptr) {
      ptr->Remove(index_);
      event_impl_.Reset();
    }
  }

  bool alive() const override { return static_cast<bool>(event_impl_); }

 private:
  RcPtrView<EventImpl<Signature, TSyncPolicy>> event_impl_;
  std::size_t index_;
};

}  // namespace events_internal

/**
 * \brief Class for remove event handlers that are not needed anymore.
 * \see Subscription for RAII wrapper.
 */
class EventHandlerDeleter {
 public:
  template <typename Signature, typename TSyncPolicy>
  EventHandlerDeleter(
      RcPtr<EventImpl<Signature, TSyncPolicy>> const& event_impl,
      std::size_t index)
      : storage_{events_internal::EventHandlerDeleterImpl(event_impl, index)},
        deleter_{storage_.ptr<events_internal::EventHandlerDeleterImplBase>()} {
  }

  EventHandlerDeleter(EventHandlerDeleter const& other);
  EventHandlerDeleter(EventHandlerDeleter&& other) noexcept;
  EventHandlerDeleter& operator=(EventHandlerDeleter const& other);
  EventHandlerDeleter& operator=(EventHandlerDeleter&& other) noexcept;

  void Delete() { deleter_->Delete(); }
  bool alive() const { return deleter_->alive(); }

 private:
  // all EventHandlerDeleterImpl has the same size
  CopyableStorage<sizeof(events_internal::EventHandlerDeleterImpl<
                         void(int), NoLockSyncPolicy>),
                  alignof(events_internal::EventHandlerDeleterImpl<
                          void(int), NoLockSyncPolicy>)>
      storage_;
  events_internal::EventHandlerDeleterImplBase* deleter_;
};

}  // namespace ae

#endif  // AETHER_EVENTS_EVENT_DELETER_H_
