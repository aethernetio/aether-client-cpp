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

#ifndef AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_RECV_ACTION_H_
#define AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_RECV_ACTION_H_

#include <optional>

#include "aether/common.h"
#include "aether/ae_context.h"
#include "aether/events/events.h"
#include "aether/actions/timer.h"
#include "aether/safe_stream/safe_stream_config.h"
#include "aether/safe_stream/details/circular_buffer.h"
#include "aether/safe_stream/details/receiving_chunk_list.h"
#include "aether/safe_stream/details/safe_stream_data_message.h"

#include "aether/tele/tele.h"

namespace ae {
class ISendAckRepeat {
 public:
  virtual ~ISendAckRepeat() = default;

  virtual void SendAck(std::uint16_t index) = 0;
  virtual void SendRepeatRequest(std::uint16_t index) = 0;
};

template <std::size_t Capacity>
class SafeStreamRecvAction {
 public:
  static constexpr std::size_t kCapacity = Capacity;
  using CircularBufferImpl = CircularBuffer<kCapacity>;
  using IndexType = CircularBufferImpl::index_type;
  using IndexRangeType = CircularBufferImpl::index_range_type;
  using ReceiveChunkListImpl = ReceiveChunkList<IndexType>;

  using ReceiveEvent = Event<void(DataBuffer&& data)>;

  SafeStreamRecvAction(AeContext const& ae_context,
                       ISendAckRepeat& send_ack_repeat,
                       SafeStreamConfig const& config)
      : ae_context_{ae_context},
        send_ack_repeat_{&send_ack_repeat},
        send_ack_timeout_{config.send_ack_timeout},
        send_repeat_timeout_{config.send_repeat_timeout},
        window_size_{config.window_size} {
    assert((window_size_ < Capacity / 2) &&
           "Window size should be less than half of capacity");
  }

  AE_CLASS_NO_COPY_MOVE(SafeStreamRecvAction)

  void PushData(std::uint16_t index, DataMessage data_message) {
    AE_TELED_DEBUG(
        "Data received index: {}, delta: {}, repeat {}, reset {}, size "
        "{}",
        index, data_message.delta_offset,
        static_cast<int>(data_message.repeat_count), data_message.reset,
        data_message.data.size());

    // The first packet received, sync with the sender
    bool reset = false;
    if (!chunks_) {
      AE_TELED_DEBUG("Init receiver");
      reset = true;
      // The sender sent first packet, sync with the sender
    } else if (data_message.reset && (session_offset != index)) {
      AE_TELED_DEBUG("Reset receiver");
      reset = true;
    }
    if (reset) {
      session_offset = index;
      buffer_.Reset(static_cast<std::size_t>(index));
      chunks_.emplace(buffer_.begin());
    }

    auto received_index =
        IndexType{static_cast<std::size_t>(index + data_message.delta_offset)};
    HandleData(received_index, data_message.repeat_count, data_message.data);
  }

  ReceiveEvent::Subscriber receive_event() {
    return EventSubscriber{receive_event_};
  }

 private:
  void HandleData(IndexType received_index, std::uint8_t repeat_count,
                  DataBuffer const& data) {
    auto received_range =
        IndexRangeType{received_index, received_index + data.size() - 1};
    auto data_span = std::span<std::uint8_t const>{data.data(), data.size()};

    if (Distance(buffer_.begin(), received_range.right) > window_size_) {
      // Got old package
      AE_TELED_DEBUG("Got old package");
      // We know nothing about the package, either it is repeated or duplicated
      // TODO: do not send ack if the package is duplicated
      EnqueueAck();
      return;
    }

    // if received range partially overlaps with buffer begin, adjust range
    if (Distance(buffer_.begin(), received_range.left) > window_size_) {
      auto dist = Distance(received_range.left, buffer_.begin());
      // make received range start from buffer begin
      received_range.left = buffer_.begin();
      // and cut the data span to match the range
      data_span = data_span.subspan(dist);
    }

    // TODO: add handle old acknowledged chunks
    auto add_res = chunks_->AddChunk(received_range, repeat_count);
    switch (add_res) {
      case ChunkAddResult::kDuplicate: {
        AE_TELED_DEBUG("Received duplicate, ignore!");
        break;
      }
      case ChunkAddResult::kAddRepeated: {
        AE_TELED_DEBUG("Received repeated chunk, enqueeu ack!");
        EnqueueAck();
        break;
      }
      default: {
        auto buffer_res = buffer_.Insert(received_range.left, data_span);
        if (!buffer_res) {
          AE_TELED_ERROR("Failed to add chunk: {}", buffer_res.error());
          // TODO: handle error!
        }
        AE_TELED_DEBUG("Received packet index: {}, size: {}, repeat_count: {}",
                       received_range.left, data_span.size(),
                       static_cast<int>(repeat_count))
        EnqueueRecv();
        break;
      }
    }
  }

  void EnqueueRecv() {
    if (recv_enqueued_) {
      return;
    }
    recv_enqueued_ = ae_context_.scheduler().Task([this]() {
      recv_enqueued_.Reset();
      HandleCompletedChains();
    });
  }

  void EnqueueAck() {
    if (ack_timer_ && !ack_timer_->is_finished()) {
      return;
    }
    ack_timer_.emplace(
        ae_context_, [this]() { HandleAcknowledgement(); }, send_ack_timeout_);
  }

  void EnqueueMissing() {
    if (missing_timer_ && !missing_timer_->is_finished()) {
      return;
    }
    missing_timer_.emplace(
        ae_context_, [this]() { HandleMissing(); }, send_repeat_timeout_);
  }

  void HandleCompletedChains() {
    if (!chunks_) {
      return;
    }
    if (chunks_->empty()) {
      return;
    }
    auto recv_range = chunks_->ReceiveChunk();
    if (recv_range.IsEmpty()) {
      // no continuous range to emit, enqueue missing and wait for repeat
      EnqueueMissing();
      return;
    }
    auto res = buffer_.Read(recv_range.left, recv_range.distance() + 1);
    if (res) {
      auto const& dspan = res.value();
      DataBuffer data_buffer(dspan.size());
      std::copy(dspan.first.begin(), dspan.first.end(), data_buffer.begin());
      if (dspan.second.size() != 0) {
        std::copy(
            dspan.second.begin(), dspan.second.end(),
            data_buffer.begin() +
                static_cast<DataBuffer::difference_type>(dspan.first.size()));
      }
      last_emitted_ = recv_range.right;
      AE_TELED_DEBUG(
          "Emitted received data range: {}-{} size: {}, last_emitted_: {}",
          recv_range.left, recv_range.right, data_buffer.size(), last_emitted_);
      receive_event_.Emit(std::move(data_buffer));

      chunks_->Acknowledge(recv_range.right);
      buffer_.Erase(recv_range.right + 1);
      chunks_->set_buffer_begin(buffer_.begin());
      EnqueueAck();
    } else {
      AE_TELED_DEBUG("Receiver chunk unsuccess {}", res.error());
      // TODO: handle errors
      EnqueueMissing();
    }
  }

  void HandleAcknowledgement() {
    auto ack_offset = static_cast<std::size_t>(last_emitted_);
    AE_TELED_DEBUG("Send acknowledgement for offset: {}", ack_offset);
    send_ack_repeat_->SendAck(static_cast<std::uint16_t>(ack_offset));
  }

  void HandleMissing() {
    if (!chunks_) {
      return;
    }
    if (auto res = chunks_->FindMissedChunk(); res) {
      AE_TELED_DEBUG("Send repeat request for offset range {}-{}", res->left,
                     res->right);
      auto request_offset = static_cast<std::size_t>(res->left);
      send_ack_repeat_->SendRepeatRequest(
          static_cast<std::uint16_t>(request_offset));
      // enqueue again
      EnqueueMissing();
    }
  }

  AeContext ae_context_;
  ISendAckRepeat* send_ack_repeat_;

  Duration send_ack_timeout_;
  Duration send_repeat_timeout_;
  std::size_t window_size_;

  CircularBufferImpl buffer_;
  std::optional<ReceiveChunkListImpl> chunks_;
  std::size_t session_offset{};  //< current session start offset
  IndexType last_emitted_{};

  TaskSubscription recv_enqueued_;
  std::optional<Timer<AeContext>> ack_timer_;
  std::optional<Timer<AeContext>> missing_timer_;

  ReceiveEvent receive_event_;
};
}  // namespace ae

#endif  // AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_RECV_ACTION_H_
