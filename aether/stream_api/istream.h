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

#ifndef AETHER_STREAM_API_ISTREAM_H_
#define AETHER_STREAM_API_ISTREAM_H_

#include <utility>
#include <type_traits>

#include "aether/common.h"
#include "aether/actions/action_view.h"

#include "aether/events/events.h"
#include "aether/transport/data_buffer.h"
#include "aether/stream_api/stream_write_action.h"

namespace ae {
struct StreamInfo {
  std::size_t max_element_size;  //< Max size of element available to write,
                                 // mostly the max packet size
  bool is_linked;                //< is stream linked somewhere
  bool is_writeble;              //< is stream writeable */
  bool is_soft_writable;         //< is stream soft writeable (!is_soft_writable
                                 //&& !is_writeble means write returns error)
};

inline bool operator==(StreamInfo const& left, StreamInfo const& right) {
  return (left.max_element_size == right.max_element_size) &&
         (left.is_linked == right.is_linked) &&
         (left.is_soft_writable == right.is_soft_writable) &&
         (left.is_writeble == right.is_writeble);
}

inline bool operator!=(StreamInfo const& left, StreamInfo const& right) {
  return !operator==(left, right);
}

/**
 * \brief Pass TIn data to write through and returns TOut data.
 * To write int and get std::string back specify IGate<int, std::string>
 */
template <typename TIn, typename TOut>
class IGate {
 public:
  using TypeIn = TIn;
  using TypeOut = TOut;

  using OutDataEvent = Event<void(TOut const& out_data)>;
  using GateUpdateEvent = Event<void()>;

  virtual ~IGate() = default;

  /**
   * \brief Make a write request action through this gate.
   * \param in_data Data to write
   * \param current_time Current time to control write timeouts
   * \return Action to control write process or subscribe to result.
   */
  virtual ActionView<StreamWriteAction> Write(TIn&& in_data,
                                              TimePoint current_time) = 0;
  /**
   * \brief New data received event.
   */
  virtual typename OutDataEvent::Subscriber out_data_event() = 0;

  /**
   * \brief Gate update event.
   */
  virtual GateUpdateEvent::Subscriber gate_update_event() = 0;

  /**
   * \brief Gate stream info
   */
  virtual StreamInfo stream_info() const = 0;
};

template <typename TIn, typename TOut, typename TWrite, typename TReadOut>
class Gate : public IGate<TIn, TOut> {
 public:
  using Base = IGate<TIn, TOut>;
  using OutGate = IGate<TWrite, TReadOut>;

  Gate() = default;
  AE_CLASS_MOVE_ONLY(Gate)

  // Link this Gate to some other Gate
  virtual void LinkOut(OutGate& out) = 0;
  virtual void Unlink() {
    out_ = nullptr;
    gate_update_event_.Emit();
  }

  typename Base::OutDataEvent::Subscriber out_data_event() override {
    return out_data_event_;
  }

  typename Base::GateUpdateEvent::Subscriber gate_update_event() override {
    return typename Base::GateUpdateEvent::Subscriber{gate_update_event_};
  }

  StreamInfo stream_info() const override {
    if (out_ == nullptr) {
      return {};
    }
    return out_->stream_info();
  }

 protected:
  OutGate* out_{};

  typename Base::OutDataEvent out_data_event_;
  typename Base::GateUpdateEvent gate_update_event_;
  Subscription out_data_subscription_;
  Subscription gate_update_subscription_;
};

// specialization for same in and out types
template <typename TIn, typename TOut>
class Gate<TIn, TOut, TIn, TOut> : public IGate<TIn, TOut> {
 public:
  using Base = IGate<TIn, TOut>;
  using OutGate = IGate<TIn, TOut>;

  Gate() = default;
  AE_CLASS_MOVE_ONLY(Gate)

  ActionView<StreamWriteAction> Write(TIn&& in_data,
                                      TimePoint current_time) override {
    assert(out_);
    return out_->Write(std::move(in_data), current_time);
  }

  typename Base::OutDataEvent::Subscriber out_data_event() override {
    return out_data_event_;
  }

  virtual void LinkOut(OutGate& out) {
    out_ = &out;

    out_data_subscription_ = out_->out_data_event().Subscribe(
        out_data_event_, MethodPtr<&Base::OutDataEvent::Emit>{});

    gate_update_subscription_ = out_->gate_update_event().Subscribe(
        gate_update_event_, MethodPtr<&Base::GateUpdateEvent::Emit>{});
    gate_update_event_.Emit();
  }

  virtual void Unlink() {
    out_ = nullptr;
    out_data_subscription_.Reset();
    gate_update_subscription_.Reset();
    gate_update_event_.Emit();
  }

  typename Base::GateUpdateEvent::Subscriber gate_update_event() override {
    return typename Base::GateUpdateEvent::Subscriber{gate_update_event_};
  }

  StreamInfo stream_info() const override {
    if (out_ == nullptr) {
      return {};
    }
    return out_->stream_info();
  }

 protected:
  OutGate* out_{};

  typename Base::OutDataEvent out_data_event_;
  typename Base::GateUpdateEvent gate_update_event_;
  Subscription out_data_subscription_;
  Subscription gate_update_subscription_;
};

template <typename GatInTIn, typename GateInTOut, typename GateOutTIn,
          typename GateOutTOut>
class Stream {
 public:
  using InGate = IGate<GatInTIn, GateInTOut>;
  using OutGate = IGate<GateOutTIn, GateOutTOut>;

  virtual ~Stream() = default;

  virtual InGate& in() = 0;
  virtual void LinkOut(OutGate& out_gate) = 0;
};

using ByteIGate = IGate<DataBuffer, DataBuffer>;
using ByteGate = Gate<DataBuffer, DataBuffer, DataBuffer, DataBuffer>;
using ByteStream = Stream<DataBuffer, DataBuffer, DataBuffer, DataBuffer>;

namespace _traits {
template <typename T, typename _ = void>
struct IsStreamType : std::false_type {};

template <typename T>
struct IsStreamType<T, std::void_t<decltype(std::declval<T&>().in())>>
    : std::true_type {};

template <typename T, typename _ = void>
struct IsLinkable : std::false_type {};

template <typename T>
struct IsLinkable<T, std::void_t<decltype(std::declval<T&>().LinkOut(
                         std::declval<typename T::OutGate&>()))>>
    : public std::true_type {};
}  // namespace _traits

// Helpers to build stream chains
/**
 * \brief Tie streams from left to right in write direction, and from right to
 * left in read direction
 */
template <typename TLeftMost, typename TNextRight, typename... TStreams>
void Tie(TLeftMost& left_most, TNextRight& next_right, TStreams&... streams) {
  if constexpr (sizeof...(TStreams) > 0) {
    Tie(next_right, streams...);
    TiePair(left_most, next_right);
  } else {
    TiePair(left_most, next_right);
  }
}

template <typename TLeft, typename TRight>
std::enable_if_t<_traits::IsLinkable<TLeft>::value &&
                 _traits::IsStreamType<TRight>::value>
TiePair(TLeft& left, TRight& right) {
  left.LinkOut(right.in());
}

template <typename TLeft, typename TRight>
std::enable_if_t<_traits::IsLinkable<TLeft>::value &&
                 !_traits::IsStreamType<TRight>::value>
TiePair(TLeft& left, TRight& right) {
  left.LinkOut(right);
}

}  // namespace ae

#endif  // AETHER_STREAM_API_ISTREAM_H_
