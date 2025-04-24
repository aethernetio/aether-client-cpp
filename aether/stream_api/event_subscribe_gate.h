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

#include "aether/common.h"
#include "aether/type_traits.h"
#include "aether/events/events.h"
#include "aether/events/event_subscription.h"

#include "aether/stream_api/istream.h"

namespace ae {
template <typename TData, typename TIn = TData>
class EventSubscribeGate : public Gate<TIn, TData, TIn, TData> {
 public:
  using BaseGate = Gate<TIn, TData, TIn, TData>;

  explicit EventSubscribeGate(
      EventSubscriber<void(TData const&)> event_subscriber)
      : event_subscriber_{event_subscriber},
        sub_{event_subscriber_.Subscribe(
            *this, MethodPtr<&EventSubscribeGate::OnData>{})} {}

  EventSubscribeGate(EventSubscribeGate const& other) = delete;

  EventSubscribeGate(EventSubscribeGate&& other) noexcept
      : event_subscriber_{other.event_subscriber_},
        sub_{event_subscriber_.Subscribe(
            *this, MethodPtr<&EventSubscribeGate::OnData>{})} {}

  ActionView<StreamWriteAction> Write(TIn&& in_data,
                                      TimePoint current_time) override {
    assert(BaseGate::out_);
    return BaseGate::out_->Write(std::move(in_data), current_time);
  }

  void LinkOut(typename BaseGate::OutGate& out) override {
    BaseGate::out_ = &out;
    BaseGate::gate_update_subscription_ =
        BaseGate::out_->gate_update_event().Subscribe(
            BaseGate::gate_update_event_,
            MethodPtr<&BaseGate::GateUpdateEvent::Emit>{});
    BaseGate::gate_update_event_.Emit();
  }

 private:
  void OnData(TData const& data) { BaseGate::out_data_event_.Emit(data); }
  EventSubscriber<void(TData const&)> event_subscriber_;
  Subscription sub_;
};

template <typename THandler, typename TData, typename TIn = TData>
class EventHandleGate : public Gate<TIn, TData, TIn, TData> {
  template <typename... Args>
  using MakeEventSignature = void(Args...);

 public:
  using BaseGate = Gate<TIn, TData, TIn, TData>;
  using HandlerSignature = FunctionSignature<THandler>;
  using EventSignature =
      typename TupleToTemplate<MakeEventSignature,
                               typename HandlerSignature::Args>::type;

  using EventSubscriberType = EventSubscriber<EventSignature>;

  explicit EventHandleGate(EventSubscriberType event_subscriber,
                           THandler handler)
      : event_subscriber_{event_subscriber},
        handler_{std::move(handler)},
        sub_{event_subscriber_.Subscribe([&](auto&&... args) {
          OnData(handler_(std::forward<decltype(args)>(args)...));
        })} {}

  EventHandleGate(EventHandleGate const& other) = delete;

  EventHandleGate(EventHandleGate&& other) noexcept
      : event_subscriber_{other.event_subscriber_},
        handler_{std::move(other.handler_)},
        sub_{event_subscriber_.Subscribe([&](auto&&... args) {
          OnData(handler_(std::forward<decltype(args)>(args)...));
        })} {}

  ActionView<StreamWriteAction> Write(TIn&& in_data,
                                      TimePoint current_time) override {
    assert(BaseGate::out_);
    return BaseGate::out_->Write(std::move(in_data), current_time);
  }

  void LinkOut(typename BaseGate::OutGate& out) override {
    BaseGate::out_ = &out;
    BaseGate::gate_update_subscription_ =
        BaseGate::out_->gate_update_event().Subscribe(
            BaseGate::gate_update_event_,
            MethodPtr<&BaseGate::GateUpdateEvent::Emit>{});
    BaseGate::gate_update_event_.Emit();
  }

 private:
  void OnData(TData const& data) { BaseGate::out_data_event_.Emit(data); }

  EventSubscriberType event_subscriber_;
  THandler handler_;
  Subscription sub_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_EVENT_SUBSCRIBE_GATE_H_
