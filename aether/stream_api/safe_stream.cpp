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
        return ActionResult::Result();
      }),
      sending_data_action_->ErrorEvent().Subscribe([this](auto const&) {
        state_.Set(State::kFailed);
        return ActionResult::Error();
      }),
      sending_data_action_->StopEvent().Subscribe([this](auto const&) {
        state_.Set(State::kStopped);
        return ActionResult::Stop();
      }));
}

// TODO: add tests for stop
void SafeStreamWriteAction::Stop() {
  if (sending_data_action_) {
    sending_data_action_->Stop();
  }
}

SafeStream::SafeStream(ActionContext action_context, SafeStreamConfig config)
    : safe_stream_action_{action_context, *this, config},
      safe_stream_api_{protocol_context_, safe_stream_action_},
      packet_send_actions_{action_context},
      stream_info_{config.max_packet_size, false, false, false, false} {
  safe_stream_action_.receive_event().Subscribe(
      *this, MethodPtr<&SafeStream::WriteOut>{});
}

ActionView<StreamWriteAction> SafeStream::Write(DataBuffer&& data) {
  return packet_send_actions_.Emplace(
      safe_stream_action_.SendData(std::move(data)));
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

ApiCallAdapter<SafeStreamApi> SafeStream::safe_stream_api() {
  return ApiCallAdapter{ApiContext{protocol_context_, safe_stream_api_}, *out_};
}

void SafeStream::WriteOut(DataBuffer const& data) {
  out_data_event_.Emit(data);
}

void SafeStream::OnStreamUpdate() {
  auto out_info = out_->stream_info();
  stream_info_.is_linked = out_info.is_linked;
  stream_info_.is_writable = out_info.is_writable;
  stream_info_.is_soft_writable = out_info.is_soft_writable;
  stream_info_.strict_size_rules = false;  // packet's will be split in a chunks

  safe_stream_action_.set_max_data_size(out_info.max_element_size);
  stream_update_event_.Emit();
}

void SafeStream::OnOutData(DataBuffer const& data) {
  AE_TELED_DEBUG("received data {}", data);
  auto api_parser = ApiParser{protocol_context_, data};
  api_parser.Parse(safe_stream_api_);
}
}  // namespace ae
