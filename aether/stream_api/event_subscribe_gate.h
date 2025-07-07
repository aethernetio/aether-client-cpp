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

#ifndef AETHER_STREAM_API_EVENT_SUBSCRIBE_GATE_H_
#define AETHER_STREAM_API_EVENT_SUBSCRIBE_GATE_H_

#include "aether/type_traits.h"
#include "aether/events/events.h"
#include "aether/events/event_subscription.h"

namespace ae {
namespace esg_internal {
template <typename... Args>
using MakeEventSignature = void(Args...);
}

/**
 * \brief Provide event subscription as a write out notify gate
 */
template <typename TData>
class EventWriteOutGate {
 public:
  explicit EventWriteOutGate(
      EventSubscriber<void(TData const&)> event_subscriber)
      : event_subscriber_{event_subscriber} {}

  EventWriteOutGate(EventWriteOutGate const& other) = delete;

  EventWriteOutGate(EventWriteOutGate&& other) noexcept
      : event_subscriber_{other.event_subscriber_} {}

  EventSubscriber<void(TData const&)> out_data_event() {
    return event_subscriber_;
  }

 private:
  EventSubscriber<void(TData const&)> event_subscriber_;
};

/**
 * \brief Provide an event subscription as a write out notify gate with
 * additional handler.
 */
template <typename THandler, typename TOutData>
class EventWriteOutHandleGate {
 public:
  using HandlerSignature = FunctionSignature<THandler>;
  using EventSignature =
      typename TypeListToTemplate<esg_internal::MakeEventSignature,
                                  typename HandlerSignature::Args>::type;

  using EventSubscriberType = EventSubscriber<EventSignature>;

  explicit EventWriteOutHandleGate(THandler handler,
                                   EventSubscriberType event_subscriber)
      : event_subscriber_{event_subscriber},
        handler_{std::move(handler)},
        sub_{event_subscriber_.Subscribe([&](auto&&... args) {
          OnData(handler_(std::forward<decltype(args)>(args)...));
        })} {}

  EventWriteOutHandleGate(EventWriteOutHandleGate const& other) = delete;

  EventWriteOutHandleGate(EventWriteOutHandleGate&& other) noexcept
      : event_subscriber_{other.event_subscriber_},
        handler_{std::move(other.handler_)},
        sub_{event_subscriber_.Subscribe([&](auto&&... args) {
          OnData(handler_(std::forward<decltype(args)>(args)...));
        })} {}

  EventSubscriber<void(TOutData const&)> out_data_event() {
    return event_subscriber_;
  }

 private:
  void OnData(TOutData const& data) { out_data_event_.Emit(data); }

  EventSubscriberType event_subscriber_;
  THandler handler_;
  Subscription sub_;
  Event<void(TOutData const&)> out_data_event_;
};

template <typename THandler,
          typename TOutData = typename FunctionSignature<THandler>::Ret>
EventWriteOutHandleGate(THandler handler,
                        EventSubscriber<typename TypeListToTemplate<
                            esg_internal::MakeEventSignature,
                            typename FunctionSignature<THandler>::Args>::type>
                            subscriber)
    -> EventWriteOutHandleGate<THandler, TOutData>;
}  // namespace ae

#endif  // AETHER_STREAM_API_EVENT_SUBSCRIBE_GATE_H_
