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

#ifndef AETHER_STREAM_API_BUFFER_STREAM_H_
#define AETHER_STREAM_API_BUFFER_STREAM_H_

#include <deque>
#include <cstddef>

#include "aether/common.h"

#include "aether/actions/action_ptr.h"
#include "aether/actions/action_context.h"
#include "aether/events/multi_subscription.h"

#include "aether/stream_api/istream.h"

#include "aether/tele/tele.h"

namespace ae {
// TODO: add Buffer stream tests

class BufferedWriteAction final : public StreamWriteAction {
 public:
  explicit BufferedWriteAction(ActionContext action_context);

  AE_CLASS_MOVE_ONLY(BufferedWriteAction)

  void Stop() override;
  // set to the sent state
  void Sent(ActionPtr<StreamWriteAction> swa);
  // drop the write action from the buffer
  void Drop();

 private:
  ActionPtr<StreamWriteAction> swa_;
  Subscription swa_sub_;
};

/**
 * \brief Buffers write requests until gate is not ready to accept them.
 */
template <typename T>
class BufferStream final : public Stream<T, T, T, T> {
  using Base = Stream<T, T, T, T>;

  struct BufferEntry {
    ActionPtr<BufferedWriteAction> bwa;
    T data;
  };

 public:
  explicit BufferStream(ActionContext action_context,
                        std::size_t buffer_max = static_cast<std::size_t>(100))
      : action_context_{action_context},
        buffer_max_{buffer_max},
        stream_info_{},
        linked_stream_info_{} {
    stream_info_.is_writable = true;
  }

  AE_CLASS_NO_COPY_MOVE(BufferStream)

  ActionPtr<StreamWriteAction> Write(T&& data) override {
    if (!stream_info_.is_writable) {
      AE_TELED_ERROR("Buffer is not writable");
      // decline write
      return ActionPtr<FailedStreamWriteAction>{action_context_};
    }
    // add to buffer either if is write buffered or buffer is not empty to
    // observe write order
    auto should_be_buffered = (stream_info_.link_state != LinkState::kLinked) ||
                              !linked_stream_info_.is_writable ||
                              !buffer_.empty();
    if (should_be_buffered) {
      return WriteToBuffer(std::move(data));
    }

    return DirectWrite(std::move(data));
  }

  StreamInfo stream_info() const override { return stream_info_; }

  void LinkOut(typename Base::OutStream& out) override {
    Base::out_ = &out;

    Base::out_data_sub_ =
        Base::out_->out_data_event().Subscribe(Base::out_data_event_);
    Base::update_sub_ = Base::out_->stream_update_event().Subscribe(
        MethodPtr<&BufferStream::UpdateStream>{this});

    UpdateStream();
  }

  void Unlink() override {
    Base::out_ = nullptr;
    Base::out_data_sub_.Reset();
    Base::update_sub_.Reset();

    linked_stream_info_ = {};
    stream_info_ = {};
    stream_info_.link_state = LinkState::kUnlinked;
    stream_info_.is_writable = true;

    // drop the buffer
    for (auto& b : buffer_) {
      b.bwa->Drop();
    }
    buffer_.clear();

    Base::stream_update_event_.Emit();
  }

  void DropLink() {
    // drop the buffer
    for (auto& b : buffer_) {
      b.bwa->Drop();
    }
    buffer_.clear();

    Base::out_ = nullptr;
    Base::out_data_sub_.Reset();
    Base::update_sub_.Reset();

    linked_stream_info_ = {};
    stream_info_ = {};
    stream_info_.link_state = LinkState::kLinkError;
    stream_info_.is_writable = true;

    Base::stream_update_event_.Emit();
  }

 private:
  ActionPtr<StreamWriteAction> WriteToBuffer(T&& data) {
    AE_TELED_DEBUG("BufferStream buffer write");
    auto& buff_entry = buffer_.emplace_back(BufferEntry{
        ActionPtr<BufferedWriteAction>{action_context_},
        std::move(data),
    });
    // Drain the buffer when the action is finished
    bwa_finished_sub_ += buff_entry.bwa->FinishedEvent().Subscribe([this]() {
      SetWriteable(true);
      DrainBuffer();
    });

    if (buffer_.size() == buffer_max_) {
      AE_TELED_WARNING("Buffer is full");
      SetWriteable(false);
    }
    return buff_entry.bwa;
  }

  ActionPtr<StreamWriteAction> DirectWrite(T&& data) {
    AE_TELED_DEBUG("BufferStream direct write");

    assert(Base::out_ && "Buffer stream is not linked");
    return Base::out_->Write(std::move(data));
  }

  void SetWriteable(bool value) {
    if (stream_info_.is_writable != value) {
      stream_info_.is_writable = value;
      Base::stream_update_event_.Emit();
    }
  }

  void UpdateStream() {
    auto out_info = Base::out_->stream_info();
    AE_TELED_DEBUG("Buffer stream link state {}", out_info.link_state);
    stream_info_.link_state = out_info.link_state;
    stream_info_.is_reliable = out_info.is_reliable;
    stream_info_.max_element_size = out_info.max_element_size;
    stream_info_.rec_element_size = out_info.rec_element_size;
    // is_writable set by buffer stream itself

    linked_stream_info_ = out_info;

    Base::stream_update_event_.Emit();

    if (stream_info_.link_state == LinkState::kLinked) {
      DrainBuffer();
    }
  }

  void DrainBuffer() {
    AE_TELED_DEBUG("BufferStream drain the buffer");
    auto it = std::begin(buffer_);
    for (; it != std::end(buffer_); ++it) {
      // last_out_stream_info_ may be updated during write buffered data \see
      // UpdateStream
      if (!linked_stream_info_.is_writable) {
        break;
      }
      it->bwa->Sent(DirectWrite(std::move(it->data)));
    }
    // erase sent buffered actions
    buffer_.erase(std::begin(buffer_), it);
  }

  ActionContext action_context_;
  std::size_t buffer_max_;

  StreamInfo stream_info_;
  StreamInfo linked_stream_info_;
  MultiSubscription bwa_finished_sub_;

  std::deque<BufferEntry> buffer_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_BUFFER_STREAM_H_
