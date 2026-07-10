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

#include "aether/warning_disable.h"

DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include <etl/circular_buffer.h>
DISABLE_WARNING_POP()

#include "aether/ae_context.h"
#include "aether-miscpp/types/small_function.h"
#include "aether/events/multi_subscription.h"
#include "aether/write_action/write_action.h"
#include "aether/write_action/failed_write_action.h"

#if DEBUG  // include tele only in debug mode
#  include "aether/tele/tele.h"
#endif

namespace ae {
#if DEBUG
#  define BW_LOG_DEBUG(...) AE_TELED_DEBUG(__VA_ARGS__)
#  define BW_LOG_WARNING(...) AE_TELED_WARNING(__VA_ARGS__)
#else
#  define BW_LOG_DEBUG(...)
#  define BW_LOG_WARNING(...)
#endif

class BufferedWriteAction final : public WriteAction {
 public:
  BufferedWriteAction() noexcept;

  AE_CLASS_MOVE_ONLY(BufferedWriteAction);

  void Stop() noexcept override;
  // set to the sent state
  void Sent(WriteAction& wa);
  // drop the write action from the buffer
  void Drop();

 private:
  WriteAction* wa_{};
  Subscription wa_sub_;
};

/**
 * \brief Buffers write requests until gate is not ready to accept them.
 */
template <typename T, std::size_t MaxSize = 10>
class BufferWrite {
  struct BufferEntry {
    BufferedWriteAction wa;
    T data;
    bool sent = false;
  };

 public:
  using DirectWriteFunc = SmallFunction<WriteAction*(T&& data), sizeof(void*)>;

  explicit BufferWrite(AeContext const& ae_context,
                       DirectWriteFunc&& direct_write)
      : ae_context_{ae_context}, direct_write_{std::move(direct_write)} {}

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
  WriteAction& Write(T&& data) {
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
    BW_LOG_WARNING("BufferWrite: drop all buffered writes");
    // drop the buffer
    for (auto& b : buffer_) {
      b.wa.Drop();
    }
    buffer_.clear();
  }

 private:
  WriteAction& WriteToBuffer(T&& data) {
    if (buffer_.full()) {
      BW_LOG_WARNING("Buffer is full");
      return FailedWrite();
    }
    buffer_.push(BufferEntry{
        BufferedWriteAction{},
        std::move(data),
    });
    auto& buff_entry = buffer_.back();
    BW_LOG_DEBUG("BufferWrite: buffer write, capacity: {}",
                 buffer_.capacity() - buffer_.size());
    // Drain the buffer when the action is finished
    bwa_finished_sub_ += buff_entry.wa.finished_event().Subscribe([this]() {
      if (!buffer_on_) {
        DrainBuffer();
      }
    });
    return buff_entry.wa;
  }

  WriteAction& DirectWrite(T&& data) {
    BW_LOG_DEBUG("BufferWrite: direct write");
    auto* write_action = direct_write_(std::move(data));
    if (write_action == nullptr) {
      return FailedWrite();
    }
    return *write_action;
  }

  /**
   * Write all buffered data in FIFO order.
   */
  void DrainBuffer() {
    BW_LOG_DEBUG("BufferWrite: drain the buffer, size {}", buffer_.size());
    // try send as many as possible
    for (auto& be : buffer_) {
      if (be.wa.is_finished()) {
        // notice! circular buffer pop is just incrementing the out pointer it
        // does not invalidate the iterators
        buffer_.pop();
        continue;
      }
      if (be.sent) {
        continue;
      }
      // buffer state might change during direct write
      if (buffer_on_) {
        break;
      }
      auto* dwa = direct_write_(std::move(be.data));
      // empty write action means no data has been written
      if (dwa == nullptr) {
        break;
      }
      be.sent = true;
      be.wa.Sent(*dwa);
    }
  }

  FailedWriteAction& FailedWrite() noexcept {
    if (failed_write_ && !failed_write_->is_finished()) {
      return *failed_write_;
    }
    return failed_write_.emplace(ae_context_);
  }

  AeContext ae_context_;
  DirectWriteFunc direct_write_;
  bool buffer_on_{true};
  etl::circular_buffer<BufferEntry, MaxSize> buffer_;
  std::optional<FailedWriteAction> failed_write_;
  MultiSubscription bwa_finished_sub_;
};

}  // namespace ae

#endif  // AETHER_WRITE_ACTION_BUFFER_WRITE_H_
