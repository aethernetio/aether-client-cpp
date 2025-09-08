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

#ifndef AETHER_STREAM_API_GATES_STREAM_H_
#define AETHER_STREAM_API_GATES_STREAM_H_

#include <tuple>
#include <utility>
#include <type_traits>

#include "aether/types/type_list.h"
#include "aether/stream_api/istream.h"
#include "aether/stream_api/gate_trait.h"
#include "aether/stream_api/tied_gates.h"

namespace ae {
namespace _tied_stream_internal {

/**
 * We need to build a stream \see Stream<TIn, TOut, TInOut, TOutIn> consistent
 * out of many gates like on the scheme:
 *
 *                                gate stream
 *       -------------------------------------------------------------
 *   TIn ----->o| write in  |o--|             |--o| write in  |o----> TInOut
 *              | left gate |   |... gates ...|   | right gate|
 *  TOut <-----o| write out |o--|             |--o| write out |o<---- TOutIn
 *       -------------------------------------------------------------
 * To find TIn we need to find left most gate with WriteIn method and take input
 * type
 * - for TOut we need to find left most gate with WriteOut method and take
 * output type, or with out_data_event and take the event argument type
 * - for TinOut we need to find riht most gate with WriteIn method and take
 * output type
 * - for TOutIn we need to find right most gate with WriteOut method and take
 * input type
 */
template <typename... TGates>
struct GateListTrait {
  template <typename... Gates>
  struct GetTIn {
    using type = void;
  };
  template <typename Left, typename... Gates>
  struct GetTIn<Left, Gates...> {
    using type = std::conditional_t<GateTrait<Left>::kHasWriteIn,
                                    typename GateTrait<Left>::WriteInInType,
                                    typename GetTIn<Gates...>::type>;
  };

  template <typename... Gates>
  struct GetTOut {
    using type = void;
  };
  template <typename Left, typename... Gates>
  struct GetTOut<Left, Gates...> {
    using type = std::conditional_t<
        // there may be WriteOut with void type
        !std::is_void_v<typename GateTrait<Left>::WriteOutOutType>,
        typename GateTrait<Left>::WriteOutOutType,
        // else use out_date_event type
        std::conditional_t<GateTrait<Left>::kHasOutDataEvent,
                           typename GateTrait<Left>::OutDataEventType,
                           typename GetTOut<Gates...>::type>>;
  };

  // this should be checked in reverse order
  template <typename... Gates>
  struct GetTInOut {
    using type = void;
  };
  template <typename Right, typename... Gates>
  struct GetTInOut<Right, Gates...> {
    using type = std::conditional_t<GateTrait<Right>::kHasWriteIn,
                                    typename GateTrait<Right>::WriteInOutType,
                                    typename GetTInOut<Gates...>::type>;
  };
  template <typename... Gates>
  using RGetTInOut = typename TypeListToTemplate<
      GetTInOut, typename ReversTypeList<TypeList<Gates...>>::type>::type;

  template <typename... Gates>
  struct GetTOutIn {
    using type = void;
  };
  template <typename Right, typename... Gates>
  struct GetTOutIn<Right, Gates...> {
    using type = std::conditional_t<GateTrait<Right>::kHasWriteOut,
                                    typename GateTrait<Right>::WriteOutInType,
                                    typename GetTOutIn<Gates...>::type>;
  };
  template <typename... Gates>
  using RGetTOutIn = typename TypeListToTemplate<
      GetTOutIn, typename ReversTypeList<TypeList<Gates...>>::type>::type;

  using TInType = typename GetTIn<TGates...>::type;
  using TOutType = typename GetTOut<TGates...>::type;
  using TInOutType = typename RGetTInOut<TGates...>::type;
  using TOutInType = typename RGetTOutIn<TGates...>::type;
};
}  // namespace _tied_stream_internal

template <typename... TGates>
class GatesStream final
    : public Stream<typename _tied_stream_internal::GateListTrait<
                        std::decay_t<TGates>...>::TInType,
                    typename _tied_stream_internal::GateListTrait<
                        std::decay_t<TGates>...>::TOutType,
                    typename _tied_stream_internal::GateListTrait<
                        std::decay_t<TGates>...>::TInOutType,
                    typename _tied_stream_internal::GateListTrait<
                        std::decay_t<TGates>...>::TOutInType> {
  struct HandleEvent {
    template <typename U>
    void operator()(U&& data) const {
      stream_->out_data_event_.Emit(std::forward<U>(data));
    }

    GatesStream* stream_;
  };

  template <typename THandler>
  static constexpr auto MakeOutDataEventSubscriptions(
      THandler&& handler, std::tuple<TGates...>& gates) {
    return std::apply(
        [&](auto&&... gates) {
          return TiedEventOutData(std::forward<THandler>(handler),
                                  std::forward<decltype(gates)>(gates)...);
        },
        gates);
  }

 public:
  using Gates = std::tuple<TGates...>;
  using Traits = _tied_stream_internal::GateListTrait<std::decay_t<TGates>...>;
  using Base = Stream<typename Traits::TInType, typename Traits::TOutType,
                      typename Traits::TInOutType, typename Traits::TOutInType>;
  using OutDataEvent = typename Base::OutDataEvent;
  using StreamUpdateEvent = typename Base::StreamUpdateEvent;
  using OutStream = typename Base::OutStream;

  using SubscriptionsList = decltype(MakeOutDataEventSubscriptions(
      std::declval<HandleEvent>(), std::declval<Gates&>()));

  explicit GatesStream(TGates... gates)
      : gates_{std::forward<TGates>(gates)...},
        out_data_event_subscription_{SubscribeToOutDataEvent()} {}

  ActionPtr<StreamWriteAction> Write(typename Base::TypeIn&& in_data) override {
    assert(Base::out_);
    return Base::out_->Write(std::apply(
        [&](auto&... gates) -> typename OutStream::TypeIn {
          return TiedWriteIn(std::move(in_data), gates...);
        },
        gates_));
  }

  // Write out data to that stream directly
  template <typename U>
  void WriteOut(U&& out_data) {
    if constexpr (!std::is_void_v<typename OutStream::TypeOut>) {
      static_assert(
          std::is_same_v<std::decay_t<U>, typename OutStream::TypeOut>,
          "Data must be of type OutStream::TypeOut");
      OutData(std::move(out_data));
    } else {
      static_assert(
          std::is_void_v<typename OutStream::TypeOut>,
          "There is no implelementation for OutStream::TypeOut == void");
    }
  }

  StreamInfo stream_info() const override {
    if (Base::out_ == nullptr) {
      return StreamInfo{};
    }
    auto info = Base::out_->stream_info();
    auto overhead = std::apply(
        [&](auto const&... gates) { return TiedOverhead(gates...); }, gates_);
    if (info.rec_element_size < overhead) {
      info.rec_element_size = 0;
    } else {
      info.rec_element_size -= overhead;
    }
    if (info.max_element_size < overhead) {
      info.max_element_size = 0;
    } else {
      info.max_element_size -= overhead;
    }
    return info;
  }

  void LinkOut(OutStream& out) override {
    Base::out_ = &out;

    Base::out_data_sub_ = Base::out_->out_data_event().Subscribe(
        *this, MethodPtr<&GatesStream::OutData>{});
    Base::update_sub_ = Base::out_->stream_update_event().Subscribe(
        Base::stream_update_event_,
        MethodPtr<&Base::StreamUpdateEvent::Emit>{});

    Base::stream_update_event_.Emit();
  }

  void Unlink() override {
    Base::out_ = nullptr;
    Base::update_sub_.Reset();
    Base::out_data_sub_.Reset();

    Base::stream_update_event_.Emit();
  }

 private:
  void OutData(typename OutStream::TypeOut const& data) {
    using WriteOutResType = std::invoke_result_t<
        decltype(&TiedWriteOut<typename OutStream::TypeOut const&, TGates...>),
        typename OutStream::TypeOut const&, TGates...>;
    if constexpr (!std::is_void_v<WriteOutResType>) {
      Base::out_data_event_.Emit(std::apply(
          [&](auto&... gates) ->
          typename Base::TypeOut { return TiedWriteOut(data, gates...); },
          gates_));
    } else {
      std::apply([&](auto&... gates) { TiedWriteOut(data, gates...); }, gates_);
    }
  }

  constexpr auto SubscribeToOutDataEvent() {
    return MakeOutDataEventSubscriptions(HandleEvent{this}, gates_);
  }

  Gates gates_;
  SubscriptionsList out_data_event_subscription_;
};

template <typename... U>
GatesStream(U&&...) -> GatesStream<U...>;
}  // namespace ae

#endif  // AETHER_STREAM_API_GATES_STREAM_H_
