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

#include <cstddef>
#include <utility>

#include "aether/stream_api/safe_stream/sending_data_action.h"

namespace ae {

SafeStreamWriteAction::SafeStreamWriteAction(
    ActionContext action_context,
    ActionView<SendingDataAction> sending_data_action)
    : StreamWriteAction(action_context),
      sending_data_action_{std::move(sending_data_action)} {
  subscriptions_.Push(
      sending_data_action_->SubscribeOnResult([this](auto const&) {
        state_.Set(State::kDone);
        Action::Result(*this);
      }),
      sending_data_action_->SubscribeOnError([this](auto const&) {
        state_.Set(State::kFailed);
        Action::Error(*this);
      }),
      sending_data_action_->SubscribeOnStop([this](auto const&) {
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

SafeStream::SafeStreamInGate::SafeStreamInGate(
    ActionContext action_context,
    ActionView<SafeStreamSendingAction> safe_stream_sending,
    std::size_t max_data_size)
    : packet_send_actions_{action_context},
      safe_stream_sending_{std::move(safe_stream_sending)},
      stream_info_{max_data_size, {}, {}, {}} {}

ActionView<StreamWriteAction> SafeStream::SafeStreamInGate::Write(
    DataBuffer&& buffer, TimePoint /* current_time */) {
  // TODO: add check for is writable
  auto action = packet_send_actions_.Emplace(
      safe_stream_sending_->SendData(std::move(buffer)));
  return action;
}

void SafeStream::SafeStreamInGate::WriteOut(DataBuffer&& buffer) {
  out_data_event_.Emit(buffer);
}

void SafeStream::SafeStreamInGate::LinkOut(OutGate& gate) {
  out_ = &gate;

  auto update_stream_info = [this]() {
    auto out_info = out_->stream_info();
    stream_info_.is_linked = out_info.is_linked;
    stream_info_.is_writeble = out_info.is_writeble;
    stream_info_.is_soft_writable = out_info.is_soft_writable;

    safe_stream_sending_->set_max_data_size(out_info.max_element_size);
    gate_update_event_.Emit();
  };

  gate_update_subscription_ =
      out_->gate_update_event().Subscribe(update_stream_info);
  update_stream_info();
}

StreamInfo SafeStream::SafeStreamInGate::stream_info() const {
  return stream_info_;
}

SafeStream::SafeStreamOutGate::SafeStreamOutGate(
    ProtocolContext& protocol_context)
    : protocol_context_{protocol_context} {}

ActionView<StreamWriteAction> SafeStream::SafeStreamOutGate::Write(
    DataBuffer&& buffer, TimePoint current_time) {
  assert(out_);
  return out_->Write(std::move(buffer), current_time);
}

void SafeStream::SafeStreamOutGate::LinkOut(OutGate& gate) {
  out_ = &gate;
  out_data_subscription_ =
      out_->out_data_event().Subscribe([this](auto const& buffer) {
        auto api_parser = ApiParser{protocol_context_, buffer};
        auto api = SafeStreamApi{};
        api_parser.Parse(api);
      });

  gate_update_subscription_ = out_->gate_update_event().Subscribe(
      gate_update_event_, MethodPtr<&GateUpdateEvent::Emit>{});
  gate_update_event_.Emit();
}

SafeStream::SafeStream(ActionContext action_context, SafeStreamConfig config)
    : action_context_{action_context},
      safe_stream_sending_{action_context_, protocol_context_, config},
      safe_stream_receiving_{action_context_, protocol_context_, config},
      in_{action_context_, safe_stream_sending_, config.max_data_size},
      out_{protocol_context_} {
  subscriptions_.Push(safe_stream_sending_.write_data_event().Subscribe(
                          *this, MethodPtr<&SafeStream::OnDataWrite>{}),
                      safe_stream_receiving_.receive_event().Subscribe(
                          in_, MethodPtr<&SafeStreamInGate::WriteOut>{}));

  subscriptions_.Push(
      safe_stream_receiving_.send_data_event().Subscribe(
          *this, MethodPtr<&SafeStream::OnDataReaderSend>{}),
      protocol_context_.MessageEvent<SafeStreamApi::Confirm>().Subscribe(
          *this, MethodPtr<&SafeStream::Confirm>{}),
      protocol_context_.MessageEvent<SafeStreamApi::RequestRepeat>().Subscribe(
          *this, MethodPtr<&SafeStream::RequestRepeatSend>{}),
      protocol_context_.MessageEvent<SafeStreamApi::Send>().Subscribe(
          *this, MethodPtr<&SafeStream::ReceiveSend>{}),
      protocol_context_.MessageEvent<SafeStreamApi::Repeat>().Subscribe(
          *this, MethodPtr<&SafeStream::ReceiveRepeat>{}));

  Tie(in_, out_);
}

ByteGate::Base& SafeStream::in() { return in_; }

void SafeStream::LinkOut(OutGate& gate) { out_.LinkOut(gate); }

void SafeStream::OnDataWrite(SafeStreamRingIndex offset, DataBuffer&& data,
                             TimePoint current_time) {
  auto write_action = out_.Write(std::move(data), current_time);

  subscriptions_.Push(
      write_action->SubscribeOnResult([this, offset](auto const& /* action */) {
        safe_stream_sending_.ReportWriteSuccess(offset);
      }));
  subscriptions_.Push(
      write_action->SubscribeOnError([this, offset](auto const& /* action */) {
        safe_stream_sending_.ReportWriteError(offset);
      }));
  subscriptions_.Push(
      write_action->SubscribeOnStop([this, offset](auto const& /* action */) {
        safe_stream_sending_.ReportWriteStopped(offset);
      }));
}

void SafeStream::OnDataReaderSend(DataBuffer&& data, TimePoint current_time) {
  out_.Write(std::move(data), current_time);
}

void SafeStream::Confirm(MessageEventData<SafeStreamApi::Confirm> const& msg) {
  safe_stream_sending_.Confirm(SafeStreamRingIndex{msg.message().offset});
}

void SafeStream::RequestRepeatSend(
    MessageEventData<SafeStreamApi::RequestRepeat> const& msg) {
  safe_stream_sending_.RequestRepeatSend(
      SafeStreamRingIndex{msg.message().offset});
}

void SafeStream::ReceiveSend(MessageEventData<SafeStreamApi::Send> const& msg) {
  safe_stream_receiving_.ReceiveSend(
      SafeStreamRingIndex{msg.message().offset},
      std::move(const_cast<SafeStreamApi::Send&>(msg.message()).data));
}

void SafeStream::ReceiveRepeat(
    MessageEventData<SafeStreamApi::Repeat> const& msg) {
  safe_stream_receiving_.ReceiveRepeat(
      SafeStreamRingIndex{msg.message().offset}, msg.message().repeat_count,
      std::move(const_cast<SafeStreamApi::Repeat&>(msg.message()).data));
}

}  // namespace ae
