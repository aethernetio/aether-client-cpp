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

#include <vector>
#include <cstddef>

#include "aether/common.h"

#include "aether/actions/action_ptr.h"
#include "aether/actions/action_context.h"
#include "aether/events/multi_subscription.h"

#include "aether/stream_api/istream.h"

#include "aether/tele/tele.h"

namespace ae {
// TODO: add Buffer stream tests
/**
 * \brief Buffers write requests until gate is not ready to accept them.
 */
template <typename T>
class BufferStream final : public Stream<T, T, T, T> {
  using Base = Stream<T, T, T, T>;

  class BufferedWriteAction final : public StreamWriteAction {
   public:
    BufferedWriteAction(ActionContext action_context, T&& data)
        : StreamWriteAction(action_context), data_{std::move(data)} {
      state_ = State::kQueued;
    }

    AE_CLASS_MOVE_ONLY(BufferedWriteAction)

    void Stop() override {
      if (write_action_) {
        write_action_->Stop();
      } else {
        state_ = State::kStopped;
        Action::Trigger();
      }
    }

    void Send(typename Base::OutStream& out_gate) {
      AE_TELED_DEBUG("Buffer send");

      is_sent_ = true;
      write_action_ = out_gate.Write(std::move(data_));
      state_changed_subscription_ =
          write_action_->state().changed_event().Subscribe([this](auto state) {
            state_ = state;
            Action::Trigger();
          });
    }

    bool is_sent() const { return is_sent_; }

   private:
    T data_;
    bool is_sent_{false};

    ActionPtr<StreamWriteAction> write_action_;
    Subscription state_changed_subscription_;
  };

 public:
  explicit BufferStream(ActionContext action_context,
                        std::size_t buffer_max = static_cast<std::size_t>(100))
      : action_context_{action_context},
        buffer_max_{buffer_max},
        stream_info_{},
        last_out_stream_info_{} {
    stream_info_.is_writable = true;
  }

  AE_CLASS_NO_COPY_MOVE(BufferStream)

  ActionPtr<StreamWriteAction> Write(T&& data) override {
    // add to buffer either if is write buffered or buffer is not empty to
    // observe write order
    auto should_be_buffered =
        !last_out_stream_info_.is_writable ||
        (last_out_stream_info_.link_state != LinkState::kLinked) ||
        !write_in_buffer_.empty();
    if (should_be_buffered) {
      if (!stream_info_.is_writable) {
        AE_TELED_ERROR("Buffer overflow");
        // decline write
        return ActionPtr<FailedStreamWriteAction>{action_context_};
      }

      return WriteToBuffer(std::move(data));
    }

    return DirectWrite(std::move(data));
  }

  StreamInfo stream_info() const override { return stream_info_; }

  void LinkOut(typename Base::OutStream& out) override {
    Base::out_ = &out;

    Base::out_data_sub_ = Base::out_->out_data_event().Subscribe(
        Base::out_data_event_, MethodPtr<&Base::OutDataEvent::Emit>{});
    Base::update_sub_ = Base::out_->stream_update_event().Subscribe(
        *this, MethodPtr<&BufferStream::UpdateStream>{});

    UpdateStream();
  }

  void Unlink() override {
    Base::out_ = nullptr;
    Base::out_data_sub_.Reset();
    Base::update_sub_.Reset();

    stream_info_.link_state = LinkState::kUnlinked;
    stream_info_.is_writable = false;
    stream_info_.is_reliable = false;
    stream_info_.max_element_size = 0;
    stream_info_.rec_element_size = 0;
    Base::stream_update_event_.Emit();
  }

 private:
  ActionPtr<StreamWriteAction> WriteToBuffer(T&& data) {
    AE_TELED_DEBUG("Buffered write");
    auto& action =
        write_in_buffer_.emplace_back(action_context_, std::move(data));
    // auto remove action on finish
    write_in_subscription_.Push(action->FinishedEvent().Subscribe([this]() {
      SetWriteable(true);
      DrainBuffer(*Base::out_);
    }));

    if (write_in_buffer_.size() == buffer_max_) {
      SetWriteable(false);
    }
    return action;
  }

  ActionPtr<StreamWriteAction> DirectWrite(T&& data) {
    AE_TELED_DEBUG("Direct write");

    assert(Base::out_);
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
    AE_TELED_DEBUG("Rec element size {}", out_info.rec_element_size);
    if (last_out_stream_info_ != out_info) {
      stream_info_.link_state = out_info.link_state;
      stream_info_.is_reliable = out_info.is_reliable;
      stream_info_.max_element_size = out_info.max_element_size;
      stream_info_.rec_element_size = out_info.rec_element_size;

      last_out_stream_info_ = out_info;
    }
    Base::stream_update_event_.Emit();

    if (stream_info_.link_state == LinkState::kLinked) {
      DrainBuffer(*Base::out_);
    }
  }

  void DrainBuffer(typename Base::OutStream& out) {
    AE_TELED_DEBUG("Buffer drain");
    auto it = std::begin(write_in_buffer_);
    for (; it != std::end(write_in_buffer_); ++it) {
      // last_out_stream_info_ may be updated during write buffered data \see
      // UpdateStream
      if (!last_out_stream_info_.is_writable) {
        break;
      }
      if ((*it)->is_sent()) {
        continue;
      }
      (*it)->Send(out);
    }
    write_in_buffer_.erase(std::begin(write_in_buffer_), it);
  }

  ActionContext action_context_;
  std::size_t buffer_max_;

  StreamInfo stream_info_;
  StreamInfo last_out_stream_info_;
  std::vector<ActionPtr<BufferedWriteAction>> write_in_buffer_;
  MultiSubscription write_in_subscription_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_BUFFER_STREAM_H_
