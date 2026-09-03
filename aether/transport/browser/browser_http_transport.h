/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_TRANSPORT_BROWSER_BROWSER_HTTP_TRANSPORT_H_
#define AETHER_TRANSPORT_BROWSER_BROWSER_HTTP_TRANSPORT_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "aether/ae_context.h"
#include "aether/common.h"
#include "aether/events/multi_subscription.h"
#include "aether/stream_api/istream.h"
#include "aether/transport/browser/browser_transport_common.h"
#include "aether/transport/data_packet_collector.h"
#include "aether/transport/packet_queue_manager.h"
#include "aether/types/address.h"
#include "aether/write_action/failed_write_action.h"

namespace ae {

/**
 * \brief ByteIStream over the opaque HTTP/HTTPS tunnel contract (v1):
 * connect / send / receive (long-poll) / close.
 */
class BrowserHttpTransport final : public ByteIStream {
 public:
  BrowserHttpTransport(AeContext const& ae_context, Endpoint endpoint);
  ~BrowserHttpTransport() override;

  AE_CLASS_NO_COPY_MOVE(BrowserHttpTransport)

  WriteAction& Write(DataBuffer&& in_data) override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  StreamInfo stream_info() const override;
  OutDataEvent::Subscriber out_data_event() override;
  void Restream() override;

 private:
  using WriteActionType = browser_transport_internal::BrowserWriteAction;
  using QueueManager =
      PacketQueueManager<WriteActionType,
                         browser_transport_internal::kBrowserPacketQueueSize>;

  void Connect();
  void CloseSession();
  void StartReceiveLoop();
  void SetLinkState(LinkState state);
  void EmitStreamUpdate();
  WriteAction& FailedWrite();

  bool TrySendFrame(DataBuffer const& frame, bool* wait, bool* async,
                    WriteActionType* self);
  void OnConnectOk(std::uint64_t generation, char const* session_id);
  void OnConnectErr(std::uint64_t generation);
  void OnSendOk(std::uint64_t generation);
  void OnSendErr(std::uint64_t generation);
  void OnReceiveData(std::uint64_t generation, std::uint8_t const* data,
                     int size);
  void OnReceiveErr(std::uint64_t generation);

  static void StaticOnConnectOk(void* user_data, std::uint32_t generation,
                                char const* session_id);
  static void StaticOnConnectErr(void* user_data, std::uint32_t generation);
  static void StaticOnSendOk(void* user_data, std::uint32_t generation);
  static void StaticOnSendErr(void* user_data, std::uint32_t generation);
  static void StaticOnReceiveData(void* user_data, std::uint32_t generation,
                                  std::uint8_t const* data, int size);
  static void StaticOnReceiveErr(void* user_data, std::uint32_t generation);

  AeContext ae_context_;
  Endpoint endpoint_;
  browser_transport_internal::GenerationGuard generation_;
  std::shared_ptr<void> lifetime_token_;

  StreamInfo stream_info_{};
  StreamUpdateEvent stream_update_event_;
  OutDataEvent out_data_event_;

  QueueManager queue_manager_;
  MultiSubscription send_action_subs_;
  std::optional<FailedWriteAction> failed_write_;

  std::mutex buffer_lock_;
  StreamDataPacketCollector data_packet_collector_;
  TaskSubscription read_event_sub_;
  TaskSubscription stream_update_sub_;
  bool read_event_pending_{false};

  std::unique_ptr<browser_transport_internal::AsyncCallbackBridge>
      callback_bridge_;
  int http_handle_{-1};
  std::string session_id_;
  bool send_in_flight_{false};
  WriteActionType* in_flight_action_{nullptr};
  bool receive_active_{false};
};

}  // namespace ae

#endif  // AETHER_TRANSPORT_BROWSER_BROWSER_HTTP_TRANSPORT_H_
