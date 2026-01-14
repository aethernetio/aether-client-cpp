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

#ifndef AETHER_WRITE_ACTION_BUFFER_WRITE_H_
#define AETHER_WRITE_ACTION_BUFFER_WRITE_H_

#include "aether/events/events.h"
#include "aether/actions/action_ptr.h"
#include "aether/types/small_function.h"
#include "aether/events/multi_subscription.h"
#include "aether/write_action/write_action.h"
#include "aether/write_action/failed_write_action.h"

#include "aether/tele/tele.h"

namespace ae {
class BufferedWriteAction final : public WriteAction {
 public:
  explicit BufferedWriteAction(ActionContext action_context);

  AE_CLASS_NO_COPY_MOVE(BufferedWriteAction)

  void Stop() override;
  // set to the sent state
  void Sent(ActionPtr<WriteAction> wa);
  // drop the write action from the buffer
  void Drop();

 private:
  ActionPtr<WriteAction> wa_;
  Subscription wa_sub_;
};

/**
 * \brief Buffers write requests until gate is not ready to accept them.
 */
template <typename T>
class BufferWrite {
  struct BufferEntry {
    ActionPtr<BufferedWriteAction> bwa;
    T data;
  };

 public:
  using DirectWriteFunc = SmallFunction<ActionPtr<WriteAction>(T&& data)>;
  using BufferDrainEvent = Event<void()>;

  explicit BufferWrite(ActionContext action_context,
                       DirectWriteFunc direct_write,
                       std::size_t buffer_max = static_cast<std::size_t>(100))
      : action_context_{action_context},
        direct_write_{std::move(direct_write)},
        buffer_max_{buffer_max},
        buffer_on_{true} {}

  AE_CLASS_NO_COPY_MOVE(BufferWrite)

  /**
   * \brief Control buffering.
   */
  void buffer_on() { buffer_on_ = true; }
  void buffer_off() {
    buffer_on_ = false;
    DrainBuffer();
  }
  bool is_buffer_on() const { return buffer_on_; }

  /**
   * \brief Write data to or through buffer.
   */
  ActionPtr<WriteAction> Write(T&& data) {
    auto should_be_buffered = buffer_on_ || !buffer_.empty();
    if (should_be_buffered) {
      return WriteToBuffer(std::move(data));
    }
    return DirectWrite(std::move(data));
  }

  /**
   * \brief Drop all buffered writes.
   */
  void Drop() {
    // drop the buffer
    for (auto& b : buffer_) {
      b.bwa->Drop();
    }
    buffer_.clear();
  }

 private:
  ActionPtr<WriteAction> WriteToBuffer(T&& data) {
    if (buffer_.size() >= buffer_max_) {
      AE_TELED_WARNING("Buffer is full");
      return ActionPtr<FailedWriteAction>{action_context_};
    }
    auto& buff_entry = buffer_.emplace_back(BufferEntry{
        ActionPtr<BufferedWriteAction>{action_context_},
        std::move(data),
    });
    AE_TELED_DEBUG("BufferWrite: buffer write, capacity: {}",
                   buffer_max_ - buffer_.size());
    // Drain the buffer when the action is finished
    bwa_finished_sub_ +=
        buff_entry.bwa->FinishedEvent().Subscribe([this]() { DrainBuffer(); });
    return buff_entry.bwa;
  }

  ActionPtr<WriteAction> DirectWrite(T&& data) {
    AE_TELED_DEBUG("BufferWrite: direct write");
    return direct_write_(std::move(data));
  }

  /**
   * Write all buffered data in FIFO order.
   */
  void DrainBuffer() {
    AE_TELED_DEBUG("BufferWrite: drain the buffer, size {}", buffer_.size());
    auto it = std::begin(buffer_);
    for (; it != std::end(buffer_); ++it) {
      // buffer state might change dursing direct write
      if (buffer_on_) {
        break;
      }
      auto wa = DirectWrite(std::move(it->data));
      // empty write action means no data has been written
      if (!wa) {
        break;
      }
      it->bwa->Sent(std::move(wa));
    }
    // erase sent buffered actions
    buffer_.erase(std::begin(buffer_), it);
  }

  ActionContext action_context_;
  DirectWriteFunc direct_write_;
  std::size_t buffer_max_;
  bool buffer_on_;
  std::deque<BufferEntry> buffer_;
  MultiSubscription bwa_finished_sub_;
};

}  // namespace ae

#endif  // AETHER_WRITE_ACTION_BUFFER_WRITE_H_
