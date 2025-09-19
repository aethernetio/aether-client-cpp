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

#include "aether/stream_api/protocol_gates.h"

#include "aether/tele/tele.h"

namespace ae {

SafeStreamWriteAction::SafeStreamWriteAction(
    ActionContext action_context,
    ActionPtr<SendingDataAction> sending_data_action)
    : StreamWriteAction(action_context),
      sending_data_action_{std::move(sending_data_action)} {
  subscriptions_.Push(
      sending_data_action_->StatusEvent().Subscribe([this](auto status) {
        status.OnResult([&]() { state_ = State::kDone; })
            .OnError([&]() { state_ = State::kFailed; })
            .OnStop([&]() { state_ = State::kStopped; });
        state_.Acquire();
        Action::Status(*this, status.result());
      }));
}

// TODO: add tests for stop
void SafeStreamWriteAction::Stop() {
  if (sending_data_action_) {
    sending_data_action_->Stop();
  }
}

SafeStream::SafeStream(ActionContext action_context, SafeStreamConfig config)
    : action_context_{action_context},
      config_{config},
      safe_stream_api_{protocol_context_, *this},
      send_action_{action_context_, *this, config_},
      recv_acion_{action_context_, *this, config_},
      stream_info_{config_.max_packet_size, config_.max_packet_size, false,
                   LinkState::kUnlinked, false} {
  recv_acion_->receive_event().Subscribe(*this,
                                         MethodPtr<&SafeStream::WriteOut>{});
}

ActionPtr<StreamWriteAction> SafeStream::Write(DataBuffer&& data) {
  return ActionPtr<SafeStreamWriteAction>{
      action_context_, send_action_->SendData(std::move(data))};
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

void SafeStream::WriteOut(DataBuffer const& data) {
  out_data_event_.Emit(data);
}

void SafeStream::OnStreamUpdate() {
  AE_TELED_DEBUG("Safe stream update");
  static constexpr std::size_t kSendOverhead =
      1 + sizeof(SSRingIndex::type) + 1 + 1 + 2;

  auto out_info = out_->stream_info();
  stream_info_.link_state = out_info.link_state;
  stream_info_.is_writable = out_info.is_writable;
  stream_info_.is_reliable = true;  // safe stream here to make stream reliable
  stream_info_.rec_element_size = out_info.rec_element_size;
  stream_info_.max_element_size = config_.max_packet_size;

  send_action_->SetMaxPayload((out_info.rec_element_size > 0)
                                  ? (out_info.rec_element_size - kSendOverhead)
                                  : 0);

  stream_update_event_.Emit();
}

void SafeStream::OnOutData(DataBuffer const& data) {
  AE_TELED_DEBUG("received data {}", data);
  auto api_parser = ApiParser{protocol_context_, data};
  api_parser.Parse(safe_stream_api_);
}

void SafeStream::Ack(SSRingIndex::type offset) {
  auto confirm_offset = SSRingIndex{offset};
  send_action_->Acknowledge(confirm_offset);
}

void SafeStream::RequestRepeat(SSRingIndex::type offset) {
  auto request_offset = SSRingIndex{offset};
  send_action_->RequestRepeat(request_offset);
}

void SafeStream::Send(SSRingIndex::type begin_offset,
                      DataMessage data_message) {
  auto received_offset = SSRingIndex{begin_offset};
  recv_acion_->PushData(received_offset, std::move(data_message));
}

ActionPtr<StreamWriteAction> SafeStream::PushData(SSRingIndex begin,
                                                  DataMessage&& data_message) {
  assert(out_);
  auto api_adapter =
      ApiCallAdapter{ApiContext{protocol_context_, safe_stream_api_}, *out_};
  api_adapter->send(static_cast<SSRingIndex::type>(begin),
                    std::move(data_message));

  return api_adapter.Flush();
}

void SafeStream::SendAck(SSRingIndex offset) {
  assert(out_);
  auto api_adapter =
      ApiCallAdapter{ApiContext{protocol_context_, safe_stream_api_}, *out_};

  api_adapter->ack(static_cast<SSRingIndex::type>(offset));
  api_adapter.Flush();
}

void SafeStream::SendRepeatRequest(SSRingIndex offset) {
  assert(out_);
  auto api_adapter =
      ApiCallAdapter{ApiContext{protocol_context_, safe_stream_api_}, *out_};

  api_adapter->request_repeat(static_cast<SSRingIndex::type>(offset));
  api_adapter.Flush();
}

}  // namespace ae
