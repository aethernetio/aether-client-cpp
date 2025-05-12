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

#include "aether/stream_api/safe_stream.h"

#include <utility>

#include "aether/stream_api/safe_stream/safe_stream_api.h"
#include "aether/stream_api/safe_stream/sending_data_action.h"

#include "aether/tele/tele.h"

namespace ae {

SafeStreamWriteAction::SafeStreamWriteAction(
    ActionContext action_context,
    ActionView<SendingDataAction> sending_data_action)
    : StreamWriteAction(action_context),
      sending_data_action_{std::move(sending_data_action)} {
  subscriptions_.Push(
      sending_data_action_->ResultEvent().Subscribe([this](auto const&) {
        state_.Set(State::kDone);
        Action::Result(*this);
      }),
      sending_data_action_->ErrorEvent().Subscribe([this](auto const&) {
        state_.Set(State::kFailed);
        Action::Error(*this);
      }),
      sending_data_action_->StopEvent().Subscribe([this](auto const&) {
        state_.Set(State::kStopped);
        Action::Stop(*this);
      }));
}

TimePoint SafeStreamWriteAction::Update(TimePoint current_time) {
  return current_time;
}

// TODO: add tests for stop
void SafeStreamWriteAction::Stop() {
  if (sending_data_action_) {
    sending_data_action_->Stop();
  }
}

SafeStream::SafeStream(ActionContext action_context, SafeStreamConfig config)
    : action_context_{action_context},
      safe_stream_api_{protocol_context_, *this},
      safe_stream_sending_{action_context_, config},
      safe_stream_receiving_{action_context_, config},
      packet_send_actions_{action_context_},
      stream_info_{config.max_data_size, {}, {}, {}} {
  safe_stream_sending_.send_event().Subscribe(
      *this, MethodPtr<&SafeStream::OnSendEvent>{});
  safe_stream_sending_.repeat_event().Subscribe(
      *this, MethodPtr<&SafeStream::OnRepeatEvent>{});

  safe_stream_receiving_.confirm_event().Subscribe(
      *this, MethodPtr<&SafeStream::OnConfirmEvent>{});
  safe_stream_receiving_.request_repeat_event().Subscribe(
      *this, MethodPtr<&SafeStream::OnRequestRepeatEvent>{});
  safe_stream_receiving_.receive_event().Subscribe(
      *this, MethodPtr<&SafeStream::WriteOut>{});
}

ActionView<StreamWriteAction> SafeStream::Write(DataBuffer&& data) {
  return packet_send_actions_.Emplace(
      safe_stream_sending_.SendData(std::move(data)));
}

StreamInfo SafeStream::stream_info() const { return stream_info_; }

void SafeStream::LinkOut(OutStream& out) {
  out_ = &out;
  update_sub_ = out_->stream_update_event().Subscribe(
      *this, MethodPtr<&SafeStream::OnStreamUpdate>{});
  out_data_sub_ = out_->out_data_event().Subscribe(
      *this, MethodPtr<&SafeStream::OnOutData>{});

  OnStreamUpdate();
}

void SafeStream::Confirm(std::uint16_t offset) {
  safe_stream_sending_.Confirm(SafeStreamRingIndex{offset});
}

void SafeStream::RequestRepeat(std::uint16_t offset) {
  safe_stream_sending_.RequestRepeatSend(SafeStreamRingIndex{offset});
}

void SafeStream::SendData(std::uint16_t offset, DataBuffer&& data) {
  safe_stream_receiving_.ReceiveSend(SafeStreamRingIndex{offset},
                                     std::move(data));
}
void SafeStream::RepeatData(std::uint16_t repeat_count, std::uint16_t offset,
                            DataBuffer&& data) {
  safe_stream_receiving_.ReceiveRepeat(SafeStreamRingIndex{offset},
                                       repeat_count, std::move(data));
}

void SafeStream::OnSendEvent(SafeStreamRingIndex offset, DataBuffer&& data) {
  assert(out_);

  auto api_context = ApiContext{protocol_context_, safe_stream_api_};
  api_context->send(static_cast<std::uint16_t>(offset), std::move(data));

  auto write_action = out_->Write(std::move(api_context));

  subscriptions_.Push(write_action->ResultEvent().Subscribe(
      [this, offset](auto const& /* action */) {
        safe_stream_sending_.ReportWriteSuccess(offset);
      }));
  subscriptions_.Push(write_action->ErrorEvent().Subscribe(
      [this, offset](auto const& /* action */) {
        safe_stream_sending_.ReportWriteError(offset);
      }));
  subscriptions_.Push(write_action->StopEvent().Subscribe(
      [this, offset](auto const& /* action */) {
        safe_stream_sending_.ReportWriteStopped(offset);
      }));
}

void SafeStream::OnRepeatEvent(SafeStreamRingIndex offset,
                               std::uint16_t repeat_count, DataBuffer&& data) {
  assert(out_);

  auto api_context = ApiContext{protocol_context_, safe_stream_api_};
  api_context->repeat(repeat_count, static_cast<std::uint16_t>(offset),
                      std::move(data));

  auto write_action = out_->Write(std::move(api_context));

  subscriptions_.Push(write_action->ResultEvent().Subscribe(
      [this, offset](auto const& /* action */) {
        safe_stream_sending_.ReportWriteSuccess(offset);
      }));
  subscriptions_.Push(write_action->ErrorEvent().Subscribe(
      [this, offset](auto const& /* action */) {
        safe_stream_sending_.ReportWriteError(offset);
      }));
  subscriptions_.Push(write_action->StopEvent().Subscribe(
      [this, offset](auto const& /* action */) {
        safe_stream_sending_.ReportWriteStopped(offset);
      }));
}

void SafeStream::OnConfirmEvent(SafeStreamRingIndex offset) {
  assert(out_);

  auto api_context = ApiContext{protocol_context_, safe_stream_api_};
  api_context->confirm(static_cast<std::uint16_t>(offset));

  out_->Write(std::move(api_context));
}

void SafeStream::OnRequestRepeatEvent(SafeStreamRingIndex offset) {
  assert(out_);

  auto api_context = ApiContext{protocol_context_, safe_stream_api_};
  api_context->request_repeat(static_cast<std::uint16_t>(offset));

  out_->Write(std::move(api_context));
}

void SafeStream::WriteOut(DataBuffer const& data) {
  out_data_event_.Emit(data);
}

void SafeStream::OnStreamUpdate() {
  auto out_info = out_->stream_info();
  stream_info_.is_linked = out_info.is_linked;
  stream_info_.is_writable = out_info.is_writable;
  stream_info_.is_soft_writable = out_info.is_soft_writable;

  safe_stream_sending_.set_max_data_size(out_info.max_element_size);
  stream_update_event_.Emit();
}

void SafeStream::OnOutData(DataBuffer const& data) {
  AE_TELED_DEBUG("received data {}", data);
  auto api_parser = ApiParser{protocol_context_, data};
  api_parser.Parse(safe_stream_api_);
}
}  // namespace ae
