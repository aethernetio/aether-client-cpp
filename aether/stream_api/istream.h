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
#include "aether/actions/action_ptr.h"

#include "aether/events/events.h"
#include "aether/types/data_buffer.h"
#include "aether/write_action/write_action.h"

namespace ae {
namespace istream_internal {
template <typename U, typename Enable = void>
struct SelectOutDataEvent {
  using type = Event<void()>;
};

template <typename U>
struct SelectOutDataEvent<U, std::enable_if_t<!std::is_void_v<U>>> {
  using type = Event<void(U const& out_data)>;
};
}  // namespace istream_internal

enum class LinkState : std::uint8_t {
  kUnlinked,
  kLinked,
  kLinkError,
};

struct StreamInfo {
  std::size_t
      rec_element_size;  //< Recommended size of element in bytes to write
  std::size_t max_element_size;  //< Max size of element to write
  bool is_reliable;  //< Is stream reliable, means if packets are garantead to
                     // be sent
  LinkState link_state;  //< link state of the stream
  bool is_writable;      //< is stream writeable
};

inline bool operator==(StreamInfo const& left, StreamInfo const& right) {
  return (left.rec_element_size == right.rec_element_size) &&
         (left.max_element_size == right.max_element_size) &&
         (left.is_reliable == right.is_reliable) &&
         (left.link_state == right.link_state) &&
         (left.is_writable == right.is_writable);
}

inline bool operator!=(StreamInfo const& left, StreamInfo const& right) {
  return !operator==(left, right);
}

template <typename TIn, typename TOut>
class IStream {
 public:
  using TypeIn = TIn;
  using TypeOut = TOut;

  using StreamUpdateEvent = Event<void()>;
  using OutDataEvent =
      typename istream_internal::SelectOutDataEvent<TOut>::type;

  virtual ~IStream() = default;

  /**
   * \brief Make a write request action through this stream.
   * \param in_data Data to write
   * \return Action to control write process or subscribe to result.
   */
  virtual ActionPtr<WriteAction> Write(TIn&& in_data) = 0;
  /**
   * \brief Stream update event.
   */
  virtual typename StreamUpdateEvent::Subscriber stream_update_event() = 0;
  /**
   * \brief Stream info
   */
  virtual StreamInfo stream_info() const = 0;
  /**
   * \brief New data received event.
   */
  virtual typename OutDataEvent::Subscriber out_data_event() = 0;

  /**
   * \brief Reconfigure the stream in case on user request.
   * This notifies lower layers about need of stream reconfiguration because the
   * higher layers detect problems.
   */
  virtual void Restream() = 0;
};

template <typename TIn, typename TOut, typename TInOut, typename TOutIn>
class Stream : public IStream<TIn, TOut> {
 public:
  using Base = IStream<TIn, TOut>;
  using OutDataEvent = typename Base::OutDataEvent;
  using StreamUpdateEvent = typename Base::StreamUpdateEvent;

  using OutStream = IStream<TInOut, TOutIn>;

  Stream() = default;
  ~Stream() override = default;

  AE_CLASS_MOVE_ONLY(Stream)

  StreamInfo stream_info() const override {
    if (out_ == nullptr) {
      return StreamInfo{};
    }
    return out_->stream_info();
  }

  typename StreamUpdateEvent::Subscriber stream_update_event() override {
    return EventSubscriber{stream_update_event_};
  }

  typename OutDataEvent::Subscriber out_data_event() override {
    return EventSubscriber{out_data_event_};
  }

  void Restream() override {
    if (out_ == nullptr) {
      return;
    }
    out_->Restream();
  }

  /**
   * \brief Link with out stream.
   */
  virtual void LinkOut(OutStream& out) = 0;
  /**
   * \brief Unlink from stream.
   */
  virtual void Unlink() = 0;

 protected:
  OutStream* out_{};
  StreamUpdateEvent stream_update_event_;
  OutDataEvent out_data_event_;
  Subscription update_sub_;
  Subscription out_data_sub_;
};

template <typename TIn, typename TOut>
class Stream<TIn, TOut, TIn, TOut> : public IStream<TIn, TOut> {
 public:
  using Base = IStream<TIn, TOut>;
  using OutDataEvent = typename Base::OutDataEvent;
  using StreamUpdateEvent = typename Base::StreamUpdateEvent;

  using OutStream = IStream<TIn, TOut>;

  Stream() = default;
  ~Stream() override = default;
  AE_CLASS_MOVE_ONLY(Stream)

  ActionPtr<WriteAction> Write(TIn&& data) override {
    assert(out_);
    return out_->Write(std::move(data));
  }

  StreamInfo stream_info() const override {
    if (out_ == nullptr) {
      return StreamInfo{};
    }
    return out_->stream_info();
  }

  typename StreamUpdateEvent::Subscriber stream_update_event() override {
    return EventSubscriber{stream_update_event_};
  }

  typename OutDataEvent::Subscriber out_data_event() override {
    return EventSubscriber{out_data_event_};
  }

  void Restream() override {
    if (out_ == nullptr) {
      return;
    }
    out_->Restream();
  }

  /**
   * \brief Link with out stream.
   */
  virtual void LinkOut(OutStream& out) {
    out_ = &out;
    update_sub_ = out_->stream_update_event().Subscribe(stream_update_event_);
    out_data_sub_ = out_->out_data_event().Subscribe(out_data_event_);
    stream_update_event_.Emit();
  }
  /**
   * \brief Unlink from stream.
   */
  virtual void Unlink() {
    out_ = nullptr;
    update_sub_.Reset();
    out_data_sub_.Reset();
    stream_update_event_.Emit();
  }

 protected:
  OutStream* out_{};
  StreamUpdateEvent stream_update_event_;
  OutDataEvent out_data_event_;
  Subscription update_sub_;
  Subscription out_data_sub_;
};

using ByteIStream = IStream<DataBuffer, DataBuffer>;
using ByteStream = Stream<DataBuffer, DataBuffer, DataBuffer, DataBuffer>;

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
void TiePair(TLeft& left, TRight& right) {
  left.LinkOut(right);
}

}  // namespace ae

#endif  // AETHER_STREAM_API_ISTREAM_H_
