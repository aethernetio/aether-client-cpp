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
      send_action_{action_context, *this},
      recv_acion_{action_context, *this},
      init_state_{},
      state_{State::kInit} {
  // set to 0 until it's changed from set_max_data_size
  config_.max_packet_size = 0;
  // get the init offset and request id
  init_state_.begin = safe_stream_internal::RandomOffset();
  init_state_.send_req_id = safe_stream_internal::RandomReqId();

  send_action_.SetConfig(config_.max_repeat_count, config_.max_packet_size,
                         config_.window_size, config_.wait_confirm_timeout);
  send_action_.SetOffset(init_state_.begin);
  recv_acion_.SetConfig(config_.window_size, config_.send_confirm_timeout,
                        config_.send_repeat_timeout);
  recv_acion_.SetOffset(init_state_.begin);
}

ActionView<SendingDataAction> SafeStreamAction::SendData(DataBuffer&& data) {
  return send_action_.SendData(std::move(data));
}

SafeStreamRecvAction::ReceiveEvent::Subscriber
SafeStreamAction::receive_event() {
  return recv_acion_.receive_event();
}

void SafeStreamAction::set_max_data_size(std::size_t max_data_size) {
  // max overhead for init message + repeat send
  // TODO: make it calculated by method signatures
  static constexpr std::size_t kOverhead =
      1 + sizeof(RequestId) + sizeof(std::uint16_t) +
      (3 * sizeof(std::uint16_t)) + 1 + sizeof(std::uint16_t) + 2;
  if (max_data_size < kOverhead) {
    config_.max_packet_size = 0;
  } else {
    config_.max_packet_size =
        static_cast<std::uint16_t>(max_data_size - kOverhead);
  }

  send_action_.SetConfig(config_.max_repeat_count, config_.max_packet_size,
                         config_.window_size, config_.wait_confirm_timeout);
}

SafeStreamAction::State SafeStreamAction::state() const { return state_; }
SafeStreamAction::InitState const& SafeStreamAction::init_state() const {
  return init_state_;
}

ActionResult SafeStreamAction::Update(TimePoint current_time) {
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
        return ActionResult::Delay(init_state_.sent_init +
                                   config_.wait_confirm_timeout);
      }
      AE_TELED_DEBUG("Wait InitAck timeout {:%S}",
                     config_.wait_confirm_timeout);
      // TODO: handle repeat count exceeded
      state_ = State::kReInit;
      [[fallthrough]];
    }
    case State::kReInit:
      // repeat send init
      SendInit();
  }
  return {};
}

void SafeStreamAction::Init(RequestId req_id, std::uint16_t repeat_count,
                            SafeStreamInit safe_stream_init) {
  AE_TELED_DEBUG(
      "Got Init request reqid {} offset {} window_size {} max_data_size {}",
      req_id, safe_stream_init.offset, safe_stream_init.window_size,
      safe_stream_init.max_packet_size);

  if (init_state_.recv_req_id == req_id) {
    AE_TELED_DEBUG("Duplicate Init request");
    if (init_state_.recv_repeat_count < repeat_count) {
      init_state_.recv_repeat_count = repeat_count;
      if (state_ == State::kInitiated) {
        // make sending init_ack to repeated init request
        state_ = State::kInitAck;
      }
    }
    return;
  }

  // if to big values are suggested
  if ((config_.window_size < safe_stream_init.window_size) ||
      (config_.max_packet_size < safe_stream_init.max_packet_size)) {
    state_ = State::kInitAckReconfigure;
    AE_TELED_DEBUG("InitAck reconfigure state");
  } else {
    state_ = State::kInitAck;
    config_.window_size = safe_stream_init.window_size;
    config_.max_packet_size = safe_stream_init.max_packet_size;
    AE_TELED_DEBUG("InitAck state");
  }

  init_state_.recv_req_id = req_id;
  init_state_.begin = SSRingIndex{safe_stream_init.offset};
  init_state_.recv_repeat_count = repeat_count;

  send_action_.SetConfig(config_.max_repeat_count, config_.max_packet_size,
                         config_.window_size, config_.wait_confirm_timeout);
  send_action_.SetOffset(init_state_.begin);

  recv_acion_.SetConfig(config_.window_size, config_.send_confirm_timeout,
                        config_.send_repeat_timeout);
  recv_acion_.SetOffset(init_state_.begin);
}

void SafeStreamAction::InitAck([[maybe_unused]] RequestId req_id,
                               SafeStreamInit safe_stream_init) {
  AE_TELED_DEBUG(
      "Got InitAck response req_id {} offset {} window_size {} max_data_size "
      "{}",
      req_id, safe_stream_init.offset, safe_stream_init.window_size,
      safe_stream_init.max_packet_size);
  if (state_ != State::kWaitInitAck) {
    AE_TELED_DEBUG("Already initiated");
    return;
  }
  // req_id must be answer to one of sent init

  assert(config_.window_size >= safe_stream_init.window_size);
  assert(config_.max_packet_size >= safe_stream_init.max_packet_size);

  if ((config_.window_size != safe_stream_init.window_size) ||
      config_.max_packet_size != safe_stream_init.max_packet_size) {
    // reconfigure
    config_.window_size = safe_stream_init.window_size;
    config_.max_packet_size = safe_stream_init.max_packet_size;

    send_action_.SetConfig(config_.max_repeat_count, config_.max_packet_size,
                           config_.window_size, config_.wait_confirm_timeout);

    recv_acion_.SetConfig(config_.window_size, config_.send_confirm_timeout,
                          config_.send_repeat_timeout);
  }

  state_ = State::kInitiated;
}

void SafeStreamAction::Confirm(std::uint16_t offset) {
  AE_TELED_DEBUG("Receive confirmed offset {}", offset);

  auto confirm_offset = SSRingIndex{offset};
  if (!send_action_.Confirm(confirm_offset)) {
    return;
  }

  if (state_ == State::kWaitInitAck) {
    AE_TELED_DEBUG("Got confirm in init state");
    state_ = State::kInitiated;
    Action::Trigger();
  }
}

void SafeStreamAction::RequestRepeat(std::uint16_t offset) {
  auto request_offset = SSRingIndex{offset};
  send_action_.RequestRepeat(request_offset);
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
  recv_acion_.PushData(std::move(data), received_offset, 0);
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

  auto received_offset = SSRingIndex{offset};
  recv_acion_.PushData(std::move(data), received_offset, repeat_count);
}

ActionView<StreamWriteAction> SafeStreamAction::PushData(
    DataChunk&& data_chunk, std::uint16_t repeat_count) {
  auto api_adapter = safe_api_provider_->safe_stream_api();
  if (state_ == State::kInit) {
    AE_TELED_DEBUG(
        "Send cumulative Init reqid {} offset {} window size {} max data size "
        "{}",
        init_state_.send_req_id, init_state_.begin, config_.window_size,
        config_.max_packet_size);

    ++init_state_.send_req_id.id;
    init_state_.sent_init = Now();
    api_adapter->init(init_state_.send_req_id, init_state_.sent_repeat_count++,
                      {static_cast<std::uint16_t>(init_state_.begin),
                       config_.window_size, config_.max_packet_size});
    state_ = State::kWaitInitAck;
  }
  if (repeat_count == 0) {
    AE_TELED_DEBUG("Send first chunk offset {} size {}", data_chunk.offset,
                   data_chunk.data.size());
    api_adapter->send(static_cast<std::uint16_t>(data_chunk.offset),
                      std::move(std::move(data_chunk).data));
  } else {
    AE_TELED_DEBUG("Send repeat count {} offset {} size {}", repeat_count,
                   data_chunk.offset, data_chunk.data.size());
    api_adapter->repeat(repeat_count,
                        static_cast<std::uint16_t>(data_chunk.offset),
                        std::move(std::move(data_chunk).data));
  }

  return api_adapter.Flush();
}

void SafeStreamAction::SendConfirm(SSRingIndex offset) {
  auto api_adapter = safe_api_provider_->safe_stream_api();
  api_adapter->confirm(static_cast<std::uint16_t>(offset));
  api_adapter.Flush();
}

void SafeStreamAction::SendRepeatRequest(SSRingIndex offset) {
  auto api_adapter = safe_api_provider_->safe_stream_api();
  api_adapter->request_repeat(static_cast<std::uint16_t>(offset));
  api_adapter.Flush();
}

void SafeStreamAction::InitAck() {
  if (config_.max_packet_size == 0) {
    return;
  }
  SendInitAck(init_state_.recv_req_id);
  state_ = State::kInitiated;
}

void SafeStreamAction::SendInit() {
  auto api_adapter = safe_api_provider_->safe_stream_api();
  ++init_state_.send_req_id.id;
  init_state_.sent_init = Now();
  state_ = State::kWaitInitAck;

  api_adapter->init(init_state_.send_req_id, init_state_.sent_repeat_count++,
                    {static_cast<std::uint16_t>(init_state_.begin),
                     config_.window_size, config_.max_packet_size});
  api_adapter.Flush();
}

void SafeStreamAction::SendInitAck(RequestId request_id) {
  AE_TELED_DEBUG(
      "Send InitAck request_id {} offset {} window_size {} max_data_size {}",
      request_id, init_state_.begin, config_.window_size,
      config_.max_packet_size);
  auto api_adapter = safe_api_provider_->safe_stream_api();
  api_adapter->init_ack(request_id,
                        {static_cast<std::uint16_t>(init_state_.begin),
                         config_.window_size, config_.max_packet_size});
  api_adapter.Flush();
}
}  // namespace ae
