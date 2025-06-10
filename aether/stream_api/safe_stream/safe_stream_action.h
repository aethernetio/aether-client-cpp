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
#include "aether/stream_api/safe_stream/safe_stream_send_action.h"
#include "aether/stream_api/safe_stream/safe_stream_recv_action.h"

namespace ae {
class SafeStreamApiProvider {
 public:
  virtual ~SafeStreamApiProvider() = default;

  virtual ApiCallAdapter<SafeStreamApi> safe_stream_api() = 0;
};

class SafeStreamAction final : public SafeStreamApiImpl,
                               public ISendDataPush,
                               public ISendConfirmRepeat,
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
    SSRingIndex begin;
    RequestId send_req_id;
    RequestId recv_req_id;
    TimePoint sent_init;
    std::uint16_t sent_repeat_count;
    std::uint16_t recv_repeat_count;
  };

  SafeStreamAction(ActionContext action_context,
                   SafeStreamApiProvider &safe_api_provider,
                   SafeStreamConfig const &config);

  AE_CLASS_NO_COPY_MOVE(SafeStreamAction)

  ActionResult Update(TimePoint current_time);

  // send data through safe stream
  ActionView<SendingDataAction> SendData(DataBuffer &&data);
  // receive data from safe stream
  SafeStreamRecvAction::ReceiveEvent::Subscriber receive_event();

  void set_max_data_size(std::size_t max_data_size);

  State state() const;
  InitState const &init_state() const;

  // Api impl methods
  void Init(RequestId req_id, std::uint16_t repeat_count,
            SafeStreamInit safe_stream_init) override;
  void InitAck(RequestId req_id, SafeStreamInit safe_stream_init) override;
  void Confirm(std::uint16_t offset) override;
  void RequestRepeat(std::uint16_t offset) override;
  void Send(std::uint16_t offset, DataBuffer &&data) override;
  void Repeat(std::uint16_t repeat_count, std::uint16_t offset,
              DataBuffer &&data) override;

  // Implement ISendDataPush
  ActionView<StreamWriteAction> PushData(DataChunk &&data_chunk,
                                         std::uint16_t repeat_count) override;

  // Implement ISendConfirmRepeat
  void SendConfirm(SSRingIndex offset) override;
  void SendRepeatRequest(SSRingIndex offset) override;

 private:
  void SendInit();
  void InitAck();
  void SendInitAck(RequestId request_id);

  SafeStreamApiProvider *safe_api_provider_;
  SafeStreamConfig config_;

  SafeStreamSendAction send_action_;
  SafeStreamRecvAction recv_acion_;

  InitState init_state_;
  State state_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_ACTION_H_
