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

#ifndef AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_ACTION_H_
#define AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_ACTION_H_

#include <cstdint>

#include "aether/common.h"
#include "aether/actions/action.h"
#include "aether/api_protocol/request_id.h"
#include "aether/events/multi_subscription.h"

#include "aether/stream_api/protocol_gates.h"
#include "aether/stream_api/safe_stream/safe_stream_api.h"
#include "aether/stream_api/safe_stream/send_data_buffer.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"
#include "aether/stream_api/safe_stream/safe_stream_config.h"
#include "aether/stream_api/safe_stream/sending_chunk_list.h"
#include "aether/stream_api/safe_stream/receiving_chunk_list.h"

namespace ae {
class SafeStreamApiProvider {
 public:
  virtual ~SafeStreamApiProvider() = default;

  virtual ApiCallAdapter<SafeStreamApi> safe_stream_api() = 0;
};

class SafeStreamAction final : public SafeStreamApiImpl,
                               public Action<SafeStreamAction> {
 public:
  enum class State : std::uint8_t {
    kInit,                // initiation required
    kWaitInitAck,         // wait initiation ack
    kInitAck,             // ready to send init ack
    kInitAckReconfigure,  // ready to send init ack but with new configuration
    kReInit,              // reinitiation is required
    kInitiated,           // initiated
  };

  struct InitState {
    RequestId send_req_id;
    RequestId recv_req_id;
    TimePoint sent_init;
  };

  struct SendState {
    SSRingIndex begin;       //< begin data offset
    SSRingIndex last_sent;   //< last offset for last sent data
    SSRingIndex last_added;  //< last offset for last sent data

    SendDataBuffer send_data_buffer;
    SendingChunkList sending_chunks;
  };

  struct ReceiveState {
    SSRingIndex begin;         //< last data offset confirmed
    SSRingIndex last_emitted;  //< last data offset emitted
    ReceiveChunkList chunks;

    std::optional<TimePoint> sent_confirm;
    std::optional<TimePoint> repeat_request;
  };

  using ReceiveEvent = Event<void(DataBuffer &&data)>;

  SafeStreamAction(ActionContext action_context,
                   SafeStreamApiProvider &safe_api_provider,
                   SafeStreamConfig const &config);

  AE_CLASS_NO_COPY_MOVE(SafeStreamAction)

  TimePoint Update(TimePoint current_time) override;

  // send data through safe stream
  ActionView<SendingDataAction> SendData(DataBuffer &&data);
  // receive data from safe stream
  ReceiveEvent::Subscriber receive_event();

  void set_max_data_size(std::size_t max_data_size);

  State state() const;
  InitState const &init_state() const;
  SendState const &send_state() const;
  ReceiveState const &receive_state() const;

  // Api impl methods
  void Init(RequestId req_id, std::uint16_t offset, std::uint16_t window_size,
            std::uint16_t max_data_size) override;
  void InitAck(RequestId req_id, std::uint16_t offset,
               std::uint16_t window_size, std::uint16_t max_data_size) override;
  void Confirm(std::uint16_t offset) override;
  void RequestRepeat(std::uint16_t offset) override;
  void Send(std::uint16_t offset, DataBuffer &&data) override;
  void Repeat(std::uint16_t repeat_count, std::uint16_t offset,
              DataBuffer &&data) override;

 private:
  TimePoint UpdateSend(TimePoint current_time);

  void SendChunk(TimePoint current_time);
  void Send(std::uint16_t repeat_count, DataChunk &&data_chunk,
            TimePoint current_time);
  void RejectSend(SendingChunk &sending_chunk);
  TimePoint SendTimeouts(TimePoint current_time);

  TimePoint UpdateRecv(TimePoint current_time);
  void ChecklCompletedChains();
  TimePoint CheckConfirmations(TimePoint current_time);
  TimePoint CheckMissing(TimePoint current_time);
  void SendConfirmation(SSRingIndex offset);
  void SendRequestRepeat(SSRingIndex offset);

  TimePoint UpdateInit(TimePoint current_time);
  void SendInit(TimePoint current_time);
  void InitAck();
  void SendInitAck(RequestId request_id);

  void MoveToOffset(SSRingIndex begin_offset);

  SafeStreamApiProvider *safe_api_provider_;
  SafeStreamConfig config_;
  InitState init_state_;
  SendState send_state_;
  ReceiveState receive_state_;
  State state_;

  ReceiveEvent receive_event_;
  MultiSubscription sending_data_subs_;
  MultiSubscription send_subs_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_ACTION_H_
