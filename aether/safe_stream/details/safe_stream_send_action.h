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

#ifndef AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_SEND_ACTION_H_
#define AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_SEND_ACTION_H_

#include <cstdlib>

#include "aether/common.h"
#include "aether/config.h"
#include "aether/ae_context.h"
#include "aether/events/events.h"
#include "aether/types/statistic_counter.h"
#include "aether/events/multi_subscription.h"
#include "aether/write_action/write_action.h"
#include "aether/safe_stream/safe_stream_config.h"
#include "aether/safe_stream/details/circular_buffer.h"
#include "aether/safe_stream/details/sending_chunk_list.h"
#include "aether/safe_stream/details/safe_stream_data_message.h"

#include "aether/tele/tele.h"

namespace ae {

namespace safe_stream_send_action_internal {
template <typename Num>
static inline Num RandomOffset() {
  // set seed once
  static bool const seed =
      (std::srand(static_cast<unsigned int>(time(nullptr))), true);
  (void)seed;
  auto value =
      static_cast<Num>(std::rand()) % (std::numeric_limits<Num>::max() / 2);
  return static_cast<Num>(value);
}
}  // namespace safe_stream_send_action_internal

class ISendDataPush {
 public:
  virtual ~ISendDataPush() = default;
  virtual WriteAction& PushData(std::uint16_t index,
                                DataMessage&& data_message) = 0;
};

// TODO: split on templated and not templated parts
template <std::size_t Capacity>
class SafeStreamSendAction {
 public:
  using ResponseStatistics =
      StatisticsCounter<Duration, AE_STATISTICS_SAFE_STREAM_WINDOW_SIZE>;

  static constexpr std::size_t kCapacity = Capacity;
  using CircularBufferImpl = CircularBuffer<kCapacity>;
  using IndexType = CircularBufferImpl::index_type;
  using IndexRangeType = RingIndexRange<IndexType>;
  using SendingChunkListImpl = SendingChunkList<IndexType>;

  using AcknowledgedEvent = Event<void(IndexType buffer_begin, IndexType end)>;
  using SendFailedEvent = Event<void(IndexType buffer_begin, IndexType end)>;
  using StoppedEvent = Event<void(IndexType buffer_begin, IndexType end)>;

  SafeStreamSendAction(AeContext const& ae_context,
                       ISendDataPush& send_data_push,
                       SafeStreamConfig const& config)
      : ae_context_{ae_context},
        send_data_push_{&send_data_push},
        max_repeat_count_{config.max_repeat_count},
        window_size_{config.window_size},
        sending_buffer_{
            safe_stream_send_action_internal::RandomOffset<std::size_t>()},
        sending_chunks_{sending_buffer_.begin()},
        last_sent_{sending_buffer_.begin()} {
    assert((window_size_ < Capacity / 2) &&
           "Window size should be less than half of capacity");
    // set first response timeout
    response_statistics_.Add(config.wait_ack_timeout);
  }

  AE_CLASS_NO_COPY_MOVE(SafeStreamSendAction)

  Result<IndexRangeType, int> SendData(std::span<std::uint8_t const> data) {
    AE_TELED_DEBUG("Send data size {}", data.size());
    auto res = sending_buffer_.Push(data);
    if (!res) {
      AE_TELED_ERROR("Got circular buffer error {}", res.error());
      return Error{1};
    }
    EnqueueSend();
    return Ok{std::move(res).value()};
  }

  /**
   * \brief Stop sending data in range.
   */
  void Stop(IndexRangeType range) {
    // Stop only chunks what are still not sending
    auto* sc = sending_chunks_.Select(range);
    if (sc != nullptr) {
      return;
    }
    AE_TELED_DEBUG("Stop range {}-{}", range.left, range.right);
    stopped_event_.Emit(sending_buffer_.begin(), range.right);
    sending_buffer_.EraseFrom(range.left);
  }

  bool Acknowledge(std::uint16_t confirm_offset) {
    auto confirm_index = IndexType(confirm_offset);
    AE_TELED_DEBUG("Receive ack for offset {} index {}", confirm_offset,
                   confirm_index);
    // if not in a current range, ignore
    if (!IndexRangeType{sending_buffer_.begin(), sending_buffer_.end()}.InRange(
            confirm_index, sending_buffer_.begin())) {
      AE_TELED_DEBUG("Ack is not in a current range");
      return false;
    }
    // receiving an ack means the other side synced its state
    init_state_ = false;

    if (!sending_chunks_.empty()) {
      auto response_duration = std::chrono::duration_cast<Duration>(
          Now() - sending_chunks_.front().send_time);
      response_statistics_.Add(response_duration);
    }

    if (IndexComparable{last_sent_, sending_buffer_.begin()} < confirm_index) {
      last_sent_ = confirm_index + 1;
    }
    acknowledged_event_.Emit(sending_buffer_.begin(), confirm_index);
    sending_buffer_.Erase(confirm_index + 1);
    sending_chunks_.RemoveUpTo(confirm_index);
    sending_chunks_.set_buffer_begin(sending_buffer_.begin());
    // enqueue to send new chunks, because data on the go size reduced
    EnqueueSend();
    // re-enqueu repeat timer for next waiting chunks
    repeat_timer_.Reset();
    EnqueueRepeatTimeout();
    return true;
  }

  void RequestRepeat(std::uint16_t request_offset) {
    auto request_index = IndexType(request_offset);
    // if not in a current range
    if (!IndexRangeType{sending_buffer_.begin(), sending_buffer_.end()}.InRange(
            request_index, sending_buffer_.begin())) {
      AE_TELED_WARNING("Request repeat send for not sent index {}",
                       request_index);
      return;
    }
    last_sent_ = request_index;
    EnqueueSend();
  }

  void SetMaxPayload(std::size_t max_payload_size) {
    AE_TELED_DEBUG("Set max payload to {}", max_payload_size);
    max_payload_size_ = max_payload_size;
    EnqueueSend();
  }

  AcknowledgedEvent::Subscriber acknowledged_event() {
    return EventSubscriber{acknowledged_event_};
  }
  StoppedEvent::Subscriber stopped_event() {
    return EventSubscriber{stopped_event_};
  }
  SendFailedEvent::Subscriber send_failed_event() {
    return EventSubscriber{send_failed_event_};
  }

 private:
  void EnqueueSend() {
    if (send_enqueued_) {
      return;
    }
    send_enqueued_ = ae_context_.scheduler().Task([this]() {
      send_enqueued_.Reset();
      SendChunk(Now());
    });
  }

  void EnqueueRepeatTimeout() {
    if (repeat_timer_) {
      return;
    }

    if (sending_chunks_.empty()) {
      return;
    }

    // Get timeout for the oldest chunk and setup delayed task
    // If when task is executed, the same chunk is still in the list,
    // it means the chunk is not acknowledged, so we need to repeat it.
    auto const& selected_sch = sending_chunks_.front();
    // wait timeout is depends on repeat_count and response statistics
    auto wait_ack_timeout =
        static_cast<double>(response_statistics_.percentile<99>().count());
    auto increase_factor = std::max(1.0, (AE_SAFE_STREAM_RTO_GROW_FACTOR *
                                          (selected_sch.repeat_count - 1)));
    auto wait_timeout = Duration{
        static_cast<Duration::rep>(wait_ack_timeout * increase_factor)};
    auto wait_time = selected_sch.send_time + wait_timeout;

    repeat_timer_ = ae_context_.scheduler().DelayedTask(
        [this, range{selected_sch.range}]() {
          repeat_timer_.Reset();
          ProcessRepeat(range);
        },
        wait_time);
  }

  void ProcessRepeat(IndexRangeType const& range) {
    if (sending_chunks_.empty()) {
      return;
    }
    AE_TELED_DEBUG("Wait ack timeout, repeat offset {}", range.left);
    // if the range was partially acknowledged, repeat from the
    // beginning
    if (sending_buffer_.begin().Distance(range.left) > window_size_) {
      last_sent_ = sending_buffer_.begin();
    } else {
      last_sent_ = range.left;
    }
    EnqueueSend();
  }

  Result<typename CircularBufferImpl::DSpan, int> GetNextChunk() {
    auto on_the_go_data_size = Distance(sending_buffer_.begin(), last_sent_);
    auto payload_size =
        std::min(max_payload_size_, window_size_ - on_the_go_data_size);
    if (payload_size == 0) {
      AE_TELED_WARNING(
          "Window size exceeded: begin: {} last_sent: {} on the go data "
          "size: {}, window size: {}",
          sending_buffer_.begin(), last_sent_, on_the_go_data_size,
          window_size_);
      // return empty span
      return Ok{typename CircularBufferImpl::DSpan{}};
    }

    // read payload_size
    auto read_res = sending_buffer_.Read(last_sent_, payload_size);
    if (read_res) {
      return Ok{read_res.value()};
    }
    if (read_res.error() == CircularBufferError::kEmptyBuffer) {
      AE_TELED_DEBUG("Send buffer is empty");
      // no data to send
      return Error{0};
    }
    AE_TELED_ERROR("Read from buffer error: {}",
                   static_cast<int>(read_res.error()));
    return Error{1};
  }

  Result<std::pair<typename CircularBufferImpl::DSpan,
                   typename SendingChunkListImpl::Chunk>,
         int>
  RegisterChunk(CircularBufferImpl::DSpan dspan, IndexRangeType chunk_range,
                TimePoint current_time) {
    auto& send_chunk = sending_chunks_.Register(chunk_range, current_time);

    send_chunk.repeat_count++;
    if (send_chunk.repeat_count > max_repeat_count_) {
      AE_TELED_ERROR("Repeat count exceeded");
      return Error{2};
    }
    return Ok{std::pair{dspan, send_chunk}};
  }

  /**
    The logic to actually send data.
     - check if max_payload_size_ is set
     - read chunk of data from the buffer starting from last_sent_ index.
     - register new sending chunk and count repeats
     - push the data to send_data_push_ and wait either for timeout or result
   */
  void SendChunk(TimePoint current_time) {
    if (max_payload_size_ == 0) {
      return;
    }
    auto res = GetNextChunk().Then([&](CircularBufferImpl::DSpan const& dspan) {
      auto chunk_index_range = IndexRangeType{
          .left = last_sent_,
          .right = last_sent_ + dspan.size() - 1,
      };
      last_sent_ += dspan.size();

      return RegisterChunk(dspan, chunk_index_range, current_time);
    });
    if (!res) {
      AE_TELED_DEBUG("Send chunk error {}", res.error());
      // If any error over empty buffer
      if (res.error() != 0) {
        AE_TELED_ERROR("Chunk send error!");
        // reject to send all chunks before the failed one
        RejectSend(last_sent_ - 1);
      }
      return;
    }

    // push chunk to sender stream api implementation
    // and wait for send result
    auto const& [dspan, send_chunk] = res.value();
    if (dspan.size() == 0) {
      AE_TELED_DEBUG("No chunks to send!");
      return;
    }

    send_subs_ +=
        PushData(dspan, send_chunk.range.left, send_chunk.repeat_count - 1)
            .status_event()
            .Subscribe([this, end_index{send_chunk.range.right}](auto status) {
              switch (status) {
                case WriteAction::Status::kSuccess:
                  break;
                case WriteAction::Status::kFail:
                case WriteAction::Status::kStop:
                  // if send failed, repeat
                  if (IndexComparable{last_sent_, sending_buffer_.begin()} >
                      end_index) {
                    last_sent_ = end_index;
                  }
                  EnqueueSend();
                  break;
              }
            });
    // enqueue next chunk send and setup repeat timeout
    EnqueueSend();
    EnqueueRepeatTimeout();
  }

  void RejectSend(IndexType end_index) {
    // send failed for a chunk
    send_failed_event_.Emit(sending_buffer_.begin(), end_index);

    sending_buffer_.Erase(end_index + 1);
    sending_chunks_.RemoveUpTo(end_index);
    sending_chunks_.set_buffer_begin(sending_buffer_.begin());
    last_sent_ = sending_buffer_.begin();
    // try sending next
    EnqueueSend();
  }

  WriteAction& PushData(CircularBufferImpl::DSpan const& dspan,
                        IndexType data_index, std::uint8_t repeat_count) {
    AE_TELED_DEBUG(
        "Send data message begin index {} data_index {} repeat count {} "
        "reset {} data size {}",
        sending_buffer_.begin(), data_index, static_cast<int>(repeat_count),
        init_state_, dspan.size());

    DataBuffer data_buffer(dspan.size());
    std::copy(dspan.first.begin(), dspan.first.end(), data_buffer.begin());
    std::copy(dspan.second.begin(), dspan.second.end(),
              data_buffer.begin() +
                  static_cast<DataBuffer::difference_type>(dspan.first.size()));

    return send_data_push_->PushData(
        static_cast<std::uint16_t>(sending_buffer_.begin()),
        DataMessage{
            init_state_,
            repeat_count,
            static_cast<std::uint16_t>(
                sending_buffer_.begin().Distance(data_index)),
            std::move(std::move(data_buffer)),
        });
  }

  AeContext ae_context_;
  ISendDataPush* send_data_push_;

  std::uint8_t max_repeat_count_{};
  std::size_t window_size_{};
  std::size_t max_payload_size_{};

  CircularBufferImpl sending_buffer_;
  SendingChunkListImpl sending_chunks_;
  IndexType last_sent_;  //< last index for last sent data
  bool init_state_{true};

  MultiSubscription sending_data_subs_;
  MultiSubscription send_subs_;
  ResponseStatistics response_statistics_;
  AcknowledgedEvent acknowledged_event_;
  StoppedEvent stopped_event_;
  SendFailedEvent send_failed_event_;
  TaskSubscription repeat_timer_;
  TaskSubscription send_enqueued_;
};
}  // namespace ae

#endif  // AETHER_SAFE_STREAM_DETAILS_SAFE_STREAM_SEND_ACTION_H_
