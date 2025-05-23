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

#include "aether/stream_api/safe_stream/safe_stream_action.h"

#include <random>
#include <algorithm>

#include "aether/tele/tele.h"

namespace ae {
namespace safe_stream_internal {

auto GetRand() {
  static std::random_device dev;
  static std::mt19937 rng(dev());
  static std::uniform_int_distribution<std::mt19937::result_type> dist6(
      1, std::numeric_limits<std::uint32_t>::max());
  return dist6(rng);
}

SSRingIndex RandomOffset() {
  return SSRingIndex{static_cast<SSRingIndex::type>(
      GetRand() % std::numeric_limits<std::uint16_t>::max())};
}
RequestId RandomReqId() {
  return RequestId{static_cast<std::uint32_t>(
      GetRand() % (std::numeric_limits<std::uint32_t>::max() / 2))};
}
};  // namespace safe_stream_internal

SafeStreamAction::SafeStreamAction(ActionContext action_context,
                                   SafeStreamApiProvider& safe_api_provider,
                                   SafeStreamConfig const& config)
    : Action{action_context},
      safe_api_provider_{&safe_api_provider},
      config_{config},
      init_state_{},
      send_state_{SSRingIndex{}, SSRingIndex{}, SSRingIndex{},
                  SendDataBuffer{action_context}, SendingChunkList{}},
      receive_state_{SSRingIndex{}, SSRingIndex{}, {}, {}, {}},
      state_{State::kInit} {
  auto begin_offset = safe_stream_internal::RandomOffset();

  // set to 0 until it's changed from set_max_data_size
  config_.max_data_size = 0;

  send_state_.begin = begin_offset;
  send_state_.last_sent = send_state_.begin;
  send_state_.last_added = send_state_.begin;

  receive_state_.begin = begin_offset;
  receive_state_.last_emitted = receive_state_.begin;
  receive_state_.begin = receive_state_.begin;

  init_state_.send_req_id = safe_stream_internal::RandomReqId();
}

ActionView<SendingDataAction> SafeStreamAction::SendData(DataBuffer&& data) {
  auto data_size = data.size();
  auto sending_data = SendingData{send_state_.last_added, std::move(data)};
  send_state_.last_added += static_cast<SSRingIndex::type>(data_size);

  auto send_action =
      send_state_.send_data_buffer.AddData(std::move(sending_data));
  sending_data_subs_.Push(
      send_action->stop_event().Subscribe([this](auto const& sending_data) {
        auto removed_size = send_state_.send_data_buffer.Stop(
            sending_data.offset, send_state_.begin);
        send_state_.last_added -= static_cast<SSRingIndex::type>(removed_size);
      }));
  return send_action;
}

SafeStreamAction::ReceiveEvent::Subscriber SafeStreamAction::receive_event() {
  return EventSubscriber{receive_event_};
}

void SafeStreamAction::set_max_data_size(std::size_t max_data_size) {
  // max overhead for init message + repeat send
  // TODO: make it calculated by method signatures
  static constexpr std::size_t kOverhead = 1 + sizeof(RequestId) +
                                           (3 * sizeof(std::uint16_t)) + 1 +
                                           sizeof(std::uint16_t) + 2;
  if (max_data_size < kOverhead) {
    config_.max_data_size = 0;
  } else {
    config_.max_data_size =
        static_cast<std::uint16_t>(max_data_size - kOverhead);
  }
}

SafeStreamAction::State SafeStreamAction::state() const { return state_; }
SafeStreamAction::InitState const& SafeStreamAction::init_state() const {
  return init_state_;
}
SafeStreamAction::SendState const& SafeStreamAction::send_state() const {
  return send_state_;
}
SafeStreamAction::ReceiveState const& SafeStreamAction::receive_state() const {
  return receive_state_;
}

TimePoint SafeStreamAction::Update(TimePoint current_time) {
  auto init_time = UpdateInit(current_time);
  auto send_time = UpdateSend(current_time);
  auto recv_time = UpdateRecv(current_time);

  auto new_time = current_time;
  for (auto const& t : {init_time, send_time, recv_time}) {
    if ((new_time == current_time) && (new_time != t)) {
      new_time = t;
    }
    if (t != current_time) {
      new_time = std::min(new_time, t);
    }
  }
  return new_time;
}

void SafeStreamAction::Init(RequestId req_id, std::uint16_t offset,
                            std::uint16_t window_size,
                            std::uint16_t max_data_size) {
  AE_TELED_DEBUG(
      "Got Init request reqid {} offset {} window_size {} max_data_size {}",
      req_id, offset, window_size, max_data_size);
  if (init_state_.recv_req_id.id == req_id) {
    AE_TELED_DEBUG("Duplicate Init request");
    // received init
    return;
  }
  // if to big values are suggested
  if ((config_.window_size < window_size) ||
      (config_.max_data_size < max_data_size)) {
    state_ = State::kInitAckReconfigure;
    AE_TELED_DEBUG("InitAck reconfigure state");
  } else {
    state_ = State::kInitAck;
    AE_TELED_DEBUG("InitAck state");
  }
  config_.window_size = std::min(config_.window_size, window_size);
  config_.max_data_size = std::min(config_.max_data_size, max_data_size);
  init_state_.recv_req_id = req_id;
  MoveToOffset(SSRingIndex{offset});
}

void SafeStreamAction::InitAck([[maybe_unused]] RequestId req_id,
                               std::uint16_t offset, std::uint16_t window_size,
                               std::uint16_t max_data_size) {
  AE_TELED_DEBUG(
      "Got InitAck response req_id {} offset {} window_size {} max_data_size "
      "{}",
      req_id, offset, window_size, max_data_size);
  if (state_ != State::kWaitInitAck) {
    AE_TELED_DEBUG("Already initiated");
    return;
  }
  // req_id must be answer to one of sent init
  assert(config_.window_size >= window_size);
  assert(config_.max_data_size >= max_data_size);

  config_.window_size = window_size;
  config_.max_data_size = max_data_size;

  MoveToOffset(SSRingIndex{offset});
  state_ = State::kInitiated;
}

void SafeStreamAction::Confirm(std::uint16_t offset) {
  AE_TELED_DEBUG("Receive confirmed offset {}", offset);

  auto confirm_offset = SSRingIndex{offset};
  if (send_state_.begin.Distance(confirm_offset) < config_.window_size) {
    if (state_ == State::kWaitInitAck) {
      AE_TELED_DEBUG("Got confirm in init state");
      state_ = State::kInitiated;
    }
    send_state_.sending_chunks.RemoveUpTo(confirm_offset, send_state_.begin);
    auto confirm_size =
        send_state_.send_data_buffer.Confirm(confirm_offset, send_state_.begin);
    send_state_.begin += static_cast<SSRingIndex::type>(confirm_size);
  }
  Action::Trigger();
}

void SafeStreamAction::RequestRepeat(std::uint16_t offset) {
  auto request_offset = SSRingIndex{offset};
  if (send_state_.last_sent(send_state_.begin) < request_offset) {
    AE_TELED_DEBUG("Request repeat send for not sent offset {}",
                   request_offset);
  }
  send_state_.last_sent = request_offset;
  Action::Trigger();
}

void SafeStreamAction::Send(std::uint16_t offset, DataBuffer&& data) {
  if (state_ == State::kInit) {
    AE_TELED_WARNING("Received data in non initiated state");
    state_ = State::kReInit;
    Action::Trigger();
    return;
  }
  if ((state_ == State::kInitAckReconfigure) ||
      (state_ == State::kWaitInitAck)) {
    AE_TELED_WARNING("State is init reconfigure or wait init");
    return;
  }
  AE_TELED_DEBUG("Data received offset {}, size {}", offset, data.size());

  auto received_offset = SSRingIndex{offset};
  if (receive_state_.begin.Distance(received_offset) > config_.window_size) {
    AE_TELED_DEBUG("Received old chunk");
    return;
  }

  receive_state_.chunks.AddChunk(
      ReceivingChunk{received_offset, std::move(data), 0},
      receive_state_.last_emitted, receive_state_.begin);
  if (!receive_state_.sent_confirm) {
    receive_state_.sent_confirm = Now();
  }
  if (!receive_state_.repeat_request) {
    receive_state_.repeat_request = Now();
  }
  Action::Trigger();
}

void SafeStreamAction::Repeat(std::uint16_t repeat_count, std::uint16_t offset,
                              DataBuffer&& data) {
  if (state_ == State::kInit) {
    AE_TELED_WARNING("Received data in non initiated state");
    state_ = State::kReInit;
    Action::Trigger();
    return;
  }
  if ((state_ == State::kInitAckReconfigure) ||
      (state_ == State::kWaitInitAck)) {
    AE_TELED_WARNING("State is init reconfigure or wait init");
    return;
  }
  auto data_size = data.size();
  auto received_offset = SSRingIndex{offset};
  AE_TELED_DEBUG("Repeat data received offset: {}, repeat {}, size {}",
                 received_offset, repeat_count, data_size);
  if (receive_state_.begin.Distance(received_offset) > config_.window_size) {
    AE_TELED_DEBUG("Received repeat for old chunk");
    return;
  }
  auto const res = receive_state_.chunks.AddChunk(
      ReceivingChunk{SSRingIndex{offset}, std::move(data), repeat_count},
      receive_state_.last_emitted, receive_state_.begin);
  if (!res || (res->repeat_count != repeat_count)) {
    // confirmed offset
    SendConfirmation(
        SSRingIndex{static_cast<SSRingIndex::type>(offset + data_size - 1)});
    return;
  }
  if (!receive_state_.sent_confirm) {
    receive_state_.sent_confirm = Now();
  }
  if (!receive_state_.repeat_request) {
    receive_state_.repeat_request = Now();
  }
  Action::Trigger();
}

TimePoint SafeStreamAction::UpdateSend(TimePoint current_time) {
  SendChunk(current_time);
  return SendTimeouts(current_time);
}

void SafeStreamAction::SendChunk(TimePoint current_time) {
  if (config_.max_data_size == 0) {
    return;
  }
  if (send_state_.begin.Distance(send_state_.last_sent +
                                 config_.max_data_size) > config_.window_size) {
    AE_TELED_DEBUG("Window size exceeded");
    return;
  }

  auto data_chunk = send_state_.send_data_buffer.GetSlice(
      send_state_.last_sent, config_.max_data_size, send_state_.begin);
  if (data_chunk.data.empty()) {
    // no data to send
    return;
  }

  send_state_.last_sent = data_chunk.offset + static_cast<SSRingIndex::type>(
                                                  data_chunk.data.size());

  auto& send_chunk = send_state_.sending_chunks.Register(
      data_chunk.offset,
      data_chunk.offset +
          static_cast<SSRingIndex::type>(data_chunk.data.size() - 1),
      current_time, send_state_.begin);

  auto repeat_count = send_chunk.repeat_count;
  send_chunk.repeat_count++;
  if (send_chunk.repeat_count > config_.max_repeat_count) {
    AE_TELED_ERROR("Repeat count exceeded");
    RejectSend(send_chunk);
    return;
  }

  Send(repeat_count, std::move(data_chunk), current_time);
}

void SafeStreamAction::Send(std::uint16_t repeat_count, DataChunk&& data_chunk,
                            TimePoint current_time) {
  auto end_offset = data_chunk.offset +
                    static_cast<SSRingIndex::type>(data_chunk.data.size());
  auto api_adapter = safe_api_provider_->safe_stream_api();
  if (state_ == State::kInit) {
    AE_TELED_DEBUG(
        "Send cumulative Init reqid {} offset {} window size {} max data size "
        "{}",
        init_state_.send_req_id, send_state_.begin, config_.window_size,
        config_.max_data_size);

    ++init_state_.send_req_id.id;
    init_state_.sent_init = current_time;
    api_adapter->init(init_state_.send_req_id,
                      static_cast<std::uint16_t>(send_state_.begin),
                      config_.window_size, config_.max_data_size);
    state_ = State::kWaitInitAck;
  }
  if (repeat_count == 0) {
    AE_TELED_DEBUG("Send first chunk offset {} size {}", data_chunk.offset,
                   data_chunk.data.size());
    api_adapter->send(static_cast<std::uint16_t>(data_chunk.offset),
                      std::move(std::move(data_chunk).data));
  } else {
    AE_TELED_DEBUG("Send repeat count {} offset {} size {}", repeat_count,
                   data_chunk.offset, data_chunk.data);
    api_adapter->repeat(repeat_count,
                        static_cast<std::uint16_t>(data_chunk.offset),
                        std::move(std::move(data_chunk).data));
  }

  auto write_action = api_adapter.Flush();

  send_subs_.Push(
      write_action->ErrorEvent().Subscribe([this, end_offset](auto const&) {
        send_state_.sending_chunks.RemoveUpTo(end_offset, send_state_.begin);
        send_state_.send_data_buffer.Reject(end_offset, send_state_.begin);
      }),
      write_action->StopEvent().Subscribe([this, end_offset](auto const&) {
        send_state_.sending_chunks.RemoveUpTo(end_offset, send_state_.begin);
        send_state_.send_data_buffer.Stop(end_offset, send_state_.begin);
      }));
}

void SafeStreamAction::RejectSend(SendingChunk& sending_chunk) {
  send_state_.send_data_buffer.Reject(sending_chunk.end_offset,
                                      send_state_.begin);
  send_state_.sending_chunks.RemoveUpTo(sending_chunk.end_offset,
                                        send_state_.begin);
}

TimePoint SafeStreamAction::SendTimeouts(TimePoint current_time) {
  if (send_state_.sending_chunks.empty()) {
    return current_time;
  }

  auto const& selected_sch = send_state_.sending_chunks.front();
  // wait timeout is depends on repeat_count
  auto d_wait_confirm_timeout =
      static_cast<double>(config_.wait_confirm_timeout.count());
  auto increase_factor = std::max(
      1.0, (AE_SAFE_STREAM_RTO_GROW_FACTOR * (selected_sch.repeat_count - 1)));
  auto wait_timeout = Duration{
      static_cast<Duration::rep>(d_wait_confirm_timeout * increase_factor)};

  if ((selected_sch.send_time + wait_timeout) < current_time) {
    // timeout
    AE_TELED_DEBUG("Wait confirm timeout {:%S}, repeat offset {}", wait_timeout,
                   selected_sch.begin_offset);
    // move offset to repeat send
    send_state_.last_sent = selected_sch.begin_offset;
    Action::Trigger();
    return current_time;
  }

  return selected_sch.send_time + wait_timeout;
}

TimePoint SafeStreamAction::UpdateRecv(TimePoint current_time) {
  ChecklCompletedChains();
  auto new_time = current_time;
  for (auto const& t :
       {CheckConfirmations(current_time), CheckMissing(current_time)}) {
    if ((new_time == current_time) && (new_time != t)) {
      new_time = t;
    }
    if (t != current_time) {
      new_time = std::min(new_time, t);
    }
  }
  return new_time;
}

void SafeStreamAction::ChecklCompletedChains() {
  auto joined_chunk =
      receive_state_.chunks.PopChunks(receive_state_.last_emitted);
  if (joined_chunk) {
    receive_state_.last_emitted +=
        static_cast<SSRingIndex::type>(joined_chunk->data.size());

    receive_event_.Emit(std::move(joined_chunk->data));
  }
}

TimePoint SafeStreamAction::CheckConfirmations(TimePoint current_time) {
  if (!receive_state_.sent_confirm) {
    return current_time;
  }
  if ((*receive_state_.sent_confirm + config_.send_confirm_timeout) >
      current_time) {
    return (*receive_state_.sent_confirm + config_.send_confirm_timeout);
  }

  if (receive_state_.last_emitted(receive_state_.begin) !=
      receive_state_.begin) {
    // confirm range [last_confirmed_offset_, last_emitted_offset_)
    SendConfirmation(receive_state_.last_emitted - 1);
    receive_state_.begin = receive_state_.last_emitted;
    receive_state_.sent_confirm = current_time;
  }
  return current_time;
}

TimePoint SafeStreamAction::CheckMissing(TimePoint current_time) {
  if (!receive_state_.repeat_request) {
    return current_time;
  }
  if ((*receive_state_.repeat_request + config_.send_repeat_timeout) >
      current_time) {
    return *receive_state_.repeat_request + config_.send_repeat_timeout;
  }

  auto missed =
      receive_state_.chunks.FindMissedChunks(receive_state_.last_emitted);

  for (auto& m : missed) {
    ++m.chunk->repeat_count;
    // TODO: add check repeat count
    AE_TELED_DEBUG("Request to repeat offset: {}", m.expected_offset);
    SendRequestRepeat(m.expected_offset);
  }
  receive_state_.repeat_request = current_time;
  return current_time;
}

void SafeStreamAction::SendConfirmation(SSRingIndex offset) {
  auto api_adapter = safe_api_provider_->safe_stream_api();
  api_adapter->confirm(static_cast<std::uint16_t>(offset));
  api_adapter.Flush();
}

void SafeStreamAction::SendRequestRepeat(SSRingIndex offset) {
  auto api_adapter = safe_api_provider_->safe_stream_api();
  api_adapter->request_repeat(static_cast<std::uint16_t>(offset));
  api_adapter.Flush();
}

TimePoint SafeStreamAction::UpdateInit(TimePoint current_time) {
  switch (state_) {
    case State::kInit:
    case State::kInitiated:
      break;
    case State::kInitAck:
    case State::kInitAckReconfigure:
      InitAck();
      break;
    case State::kWaitInitAck: {
      if ((init_state_.sent_init + config_.wait_confirm_timeout) >
          current_time) {
        return init_state_.sent_init + config_.wait_confirm_timeout;
      }
      AE_TELED_DEBUG("Wait InitAck timeout {:%S}",
                     config_.wait_confirm_timeout);
      // TODO: handle repeat count exceeded
      state_ = State::kReInit;
      [[fallthrough]];
    }
    case State::kReInit:
      // repeat send init
      SendInit(current_time);
  }
  return current_time;
}

void SafeStreamAction::InitAck() {
  if (config_.max_data_size == 0) {
    return;
  }
  SendInitAck(init_state_.recv_req_id);
  state_ = State::kInitiated;
}

void SafeStreamAction::SendInit(TimePoint current_time) {
  auto api_adapter = safe_api_provider_->safe_stream_api();
  ++init_state_.send_req_id.id;
  init_state_.sent_init = current_time;
  state_ = State::kWaitInitAck;

  api_adapter->init(init_state_.send_req_id,
                    static_cast<std::uint16_t>(send_state_.begin),
                    config_.window_size, config_.max_data_size);
  api_adapter.Flush();
}

void SafeStreamAction::SendInitAck(RequestId request_id) {
  AE_TELED_DEBUG(
      "Send InitAck request_id {} offset {} window_size {} max_data_size {}",
      request_id, send_state_.begin, config_.window_size,
      config_.max_data_size);
  auto api_adapter = safe_api_provider_->safe_stream_api();
  api_adapter->init_ack(request_id,
                        static_cast<std::uint16_t>(send_state_.begin),
                        config_.window_size, config_.max_data_size);
  api_adapter.Flush();
}

void SafeStreamAction::MoveToOffset(SSRingIndex begin_offset) {
  auto send_distance = send_state_.begin.Distance(begin_offset);
  send_state_.begin = begin_offset;
  send_state_.last_added += send_distance;
  send_state_.last_sent += send_distance;
  send_state_.send_data_buffer.MoveOffset(send_distance);
  send_state_.sending_chunks.MoveOffset(send_distance);

  receive_state_.begin = begin_offset;
  receive_state_.last_emitted = begin_offset;
  receive_state_.chunks.Clear();
}

}  // namespace ae
