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

#include "aether/stream_api/buffer_stream.h"

#include "aether/tele/tele.h"

namespace ae {

BufferStream::BufferedWriteAction::BufferedWriteAction(
    ActionContext action_context, DataBuffer&& data)
    : StreamWriteAction(action_context),
      data_{std::move(data)},
      data_size_{data_.size()} {
  state_ = State::kQueued;
}

TimePoint BufferStream::BufferedWriteAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kStopped:
        Action::Stop(*this);
        break;
      default:
        break;
    }
  }

  return current_time;
}

void BufferStream::BufferedWriteAction::Stop() {
  if (write_action_) {
    write_action_->Stop();
  } else {
    state_ = State::kStopped;
  }
  Action::Trigger();
}

void BufferStream::BufferedWriteAction::Send(OutStream& out_gate) {
  is_sent_ = true;

  write_action_ = out_gate.Write(std::move(data_));
  state_changed_subscription_ =
      write_action_->state().changed_event().Subscribe(
          [this](auto state) { state_ = state; });

  write_action_subscription_.Push(
      write_action_->ResultEvent().Subscribe([this](auto const&) {
        state_.Acquire();
        Action::Result(*this);
      }),
      write_action_->ErrorEvent().Subscribe([this](auto const&) {
        state_.Acquire();
        Action::Error(*this);
      }),
      write_action_->StopEvent().Subscribe([this](auto const&) {
        state_.Acquire();
        Action::Stop(*this);
      }));
}

bool BufferStream::BufferedWriteAction::is_sent() const { return is_sent_; }

std::size_t BufferStream::BufferedWriteAction::size() const {
  return data_size_;
}

BufferStream::BufferStream(ActionContext action_context, std::size_t buffer_max)
    : action_context_{action_context},
      buffer_max_{buffer_max},
      stream_info_{},
      last_out_stream_info_{},
      failed_write_list_{action_context_} {
  stream_info_.is_soft_writable = true;
}

ActionView<StreamWriteAction> BufferStream::Write(DataBuffer&& data) {
  auto add_to_buffer =
      !last_out_stream_info_.is_writable || !last_out_stream_info_.is_linked;
  // add to buffer either if is write buffered or buffer is not empty to observe
  // write order
  if (add_to_buffer || !write_in_buffer_.empty()) {
    if (!stream_info_.is_soft_writable) {
      AE_TELED_ERROR("Buffer overflow");
      // decline write
      return failed_write_list_.Emplace();
    }

    AE_TELED_DEBUG("Make a buffered write");

    auto action_it = write_in_buffer_.emplace(std::end(write_in_buffer_),
                                              action_context_, std::move(data));
    auto remove_action = [this, action_it]() {
      write_in_buffer_.erase(action_it);
      SetSoftWriteable(true);
      DrainBuffer(*out_);
    };
    write_in_subscription_.Push(
        action_it->FinishedEvent().Subscribe(remove_action));

    if (write_in_buffer_.size() == buffer_max_) {
      SetSoftWriteable(false);
    }

    return *action_it;
  }

  AE_TELED_DEBUG("Make a direct write");
  assert(out_);
  return out_->Write(std::move(data));
}

StreamInfo BufferStream::stream_info() const { return stream_info_; }

void BufferStream::LinkOut(OutStream& out) {
  out_ = &out;

  out_data_sub_ = out_->out_data_event().Subscribe(
      out_data_event_, MethodPtr<&OutDataEvent::Emit>{});
  update_sub_ = out_->stream_update_event().Subscribe(
      *this, MethodPtr<&BufferStream::UpdateGate>{});

  UpdateGate();
}

void BufferStream::Unlink() {
  out_ = nullptr;
  out_data_sub_.Reset();
  update_sub_.Reset();

  stream_info_.is_linked = false;
  stream_info_.is_writable = false;
  stream_info_.max_element_size = 0;
  stream_info_.is_soft_writable = write_in_buffer_.size() < buffer_max_;
  stream_update_event_.Emit();
}

void BufferStream::SetSoftWriteable(bool value) {
  if (stream_info_.is_soft_writable != value) {
    stream_info_.is_soft_writable = value;
    stream_update_event_.Emit();
  }
}

void BufferStream::UpdateGate() {
  auto out_info = out_->stream_info();
  if (last_out_stream_info_ != out_info) {
    stream_info_.is_linked = out_info.is_linked;
    stream_info_.max_element_size = out_info.max_element_size;
    stream_info_.is_writable = out_info.is_writable;

    last_out_stream_info_ = out_info;

    stream_update_event_.Emit();
  }

  if (stream_info_.is_linked) {
    DrainBuffer(*out_);
  }
}

void BufferStream::DrainBuffer(OutStream& out) {
  for (auto& action : write_in_buffer_) {
    if (!last_out_stream_info_.is_writable) {
      break;
    }
    if (action.is_sent()) {
      continue;
    }
    AE_TELED_DEBUG("Buffer drain");
    action.Send(out);
  }
}

}  // namespace ae
