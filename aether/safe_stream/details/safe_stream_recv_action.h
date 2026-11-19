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

#include "aether/ae_context.h"
#include "aether/common.h"
#include "aether/safe_stream/details/circular_buffer.h"
#include "aether/safe_stream/details/receiving_chunk_list.h"
#include "aether/safe_stream/details/safe_stream_data_message.h"
#include "aether/safe_stream/safe_stream_config.h"

#include "aether/tele.h"

namespace ae {
template <typename Offset>
class SafeStreamRecvActionDelegate {
 public:
  virtual ~SafeStreamRecvActionDelegate() = default;

  virtual void SendAck(Offset index) = 0;
  virtual void SendRepeatRequest(Offset index) = 0;
  virtual void ReceiveData(DataBuffer& data) = 0;
};

using ISafeStreamRecvActionDelegate = SafeStreamRecvActionDelegate<std::uint16_t>;

template <std::size_t Capacity, typename Offset = std::uint16_t>
class SafeStreamRecvAction {
 public:
  static constexpr std::size_t kCapacity = Capacity;
  using CircularBufferImpl = SafeStreamBuffer<kCapacity, Offset>;
  using IndexType = CircularBufferImpl::index_type;
  using IndexRangeType = CircularBufferImpl::index_range_type;
  using ReceiveChunkListImpl = ReceiveChunkList<IndexType>;

  SafeStreamRecvAction(AeContext const& ae_context,
                       SafeStreamRecvActionDelegate<Offset>& delegate,
                       SafeStreamConfig const& config)
      : ae_context_{ae_context},
        delegate_{&delegate},
        send_ack_timeout_{config.send_ack_timeout},
        send_repeat_timeout_{config.send_repeat_timeout},
        window_size_{config.window_size},
        ack_batch_bytes_{std::min(config.ack_batch_bytes, config.window_size)} {
    assert((window_size_ < Capacity / 2) &&
           "Window size should be less than half of capacity");
  }

  AE_CLASS_NO_COPY_MOVE(SafeStreamRecvAction)

  std::optional<std::size_t> receive_session_offset() const {
    return chunks_ ? std::optional{session_offset} : std::nullopt;
  }

  void PushData(Offset index, SafeStreamDataMessage<Offset> data_message) {
    AETHER_TELED_DEBUG(
        "Data received index: {}, delta: {}, repeat {}, reset {}, size "
        "{}",
        index, data_message.delta_offset,
        static_cast<int>(data_message.repeat_count), data_message.reset,
        data_message.data.size());

    // The first packet received, sync with the sender
    bool reset = false;
    if (!chunks_) {
      AETHER_TELED_DEBUG("Init receiver");
      reset = true;
      // The sender sent first packet, sync with the sender
    } else if (data_message.reset && (session_offset != index)) {
      AETHER_TELED_DEBUG("Reset receiver");
      reset = true;
    }
    if (reset) {
      ack_timer_.Reset();
      ack_urgent_ = false;
      pending_ack_bytes_ = 0;
      last_emitted_.reset();
      session_offset = index;
      buffer_.Reset(static_cast<std::size_t>(index));
      chunks_.emplace(buffer_.begin());
    }

    auto received_index =
        IndexType{static_cast<std::size_t>(index + data_message.delta_offset)};
    HandleData(received_index, data_message.repeat_count, data_message.data);
  }

 private:
  void HandleData(IndexType received_index, std::uint8_t repeat_count,
                  DataBuffer const& data) {
    auto received_range =
        IndexRangeType{received_index, received_index + data.size() - 1};
    auto data_span = std::span<std::uint8_t const>{data.data(), data.size()};

    if (Distance(buffer_.begin(), received_range.right) > window_size_) {
      // Got old package
      AETHER_TELED_DEBUG("Got old package");
      // We know nothing about the package, either it is repeated or duplicated
      // TODO: do not send ack if the package is duplicated
      EnqueueAck(ack_batch_bytes_ != 0 && repeat_count != 0);
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
        AETHER_TELED_DEBUG("Received duplicate, ignore!");
        break;
      }
      case ChunkAddResult::kAddRepeated: {
        AETHER_TELED_DEBUG("Received repeated chunk, enqueeu ack!");
        EnqueueAck(ack_batch_bytes_ != 0);
        break;
      }
      default: {
        auto buffer_res = buffer_.Insert(received_range.left, data_span);
        if (!buffer_res) {
          AETHER_TELED_ERROR("Failed to add chunk: {}", buffer_res.error());
          // TODO: handle error!
        }
        AETHER_TELED_DEBUG("Received packet index: {}, size: {}, repeat_count: {}",
                       received_range.left, data_span.size(),
                       static_cast<int>(repeat_count));
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

  void EnqueueAck(bool urgent = false) {
    if (!last_emitted_ || (ack_timer_ && (!urgent || ack_urgent_))) {
      return;
    }
    ack_timer_.Reset();
    ack_urgent_ = urgent;
    ack_timer_ = ae_context_.scheduler().DelayedTask(
        [this]() { HandleAcknowledgement(); },
        urgent ? Duration::zero() : send_ack_timeout_);
  }

  void EnqueueMissing() {
    if (missing_timer_) {
      return;
    }
    missing_timer_ = ae_context_.scheduler().DelayedTask(
        [this]() { HandleMissing(); }, send_repeat_timeout_);
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
      AETHER_TELED_DEBUG(
          "Emitted received data range: {}-{} size: {}, last_emitted_: {}",
          recv_range.left, recv_range.right, data_buffer.size(), *last_emitted_);
      delegate_->ReceiveData(data_buffer);

      chunks_->Acknowledge(recv_range.right);
      buffer_.Erase(recv_range.right + 1);
      chunks_->set_buffer_begin(buffer_.begin());
      if (ack_batch_bytes_ != 0) {
        pending_ack_bytes_ += recv_range.distance() + 1;
      }
      EnqueueAck(ack_batch_bytes_ != 0 && pending_ack_bytes_ >= ack_batch_bytes_);
    } else {
      AETHER_TELED_DEBUG("Receiver chunk unsuccess {}", res.error());
      // TODO: handle errors
      EnqueueMissing();
    }
  }

  void HandleAcknowledgement() {
    ack_timer_.Reset();
    ack_urgent_ = false;
    pending_ack_bytes_ = 0;
    auto ack_offset = static_cast<std::size_t>(*last_emitted_);
    AETHER_TELED_DEBUG("Send acknowledgement for offset: {}", ack_offset);
    delegate_->SendAck(static_cast<Offset>(ack_offset));
  }

  void HandleMissing() {
    if (!chunks_) {
      return;
    }
    if (auto res = chunks_->FindMissedChunk(); res) {
      AETHER_TELED_DEBUG("Send repeat request for offset range {}-{}", res->left,
                     res->right);
      auto request_offset = static_cast<std::size_t>(res->left);
      delegate_->SendRepeatRequest(static_cast<Offset>(request_offset));
      // enqueue again
      missing_timer_.Reset();
      EnqueueMissing();
    }
  }

  AeContext ae_context_;
  SafeStreamRecvActionDelegate<Offset>* delegate_;

  Duration send_ack_timeout_;
  Duration send_repeat_timeout_;
  std::size_t window_size_;
  std::size_t ack_batch_bytes_;
  std::size_t pending_ack_bytes_{};
  bool ack_urgent_{};

  CircularBufferImpl buffer_;
  std::optional<ReceiveChunkListImpl> chunks_;
  std::size_t session_offset{};  //< current session start offset
  std::optional<IndexType> last_emitted_;

  TaskSubscription recv_enqueued_;
  TaskSubscription ack_timer_;
  TaskSubscription missing_timer_;
};
}  // namespace ae

#endif  // AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_RECV_ACTION_H_
