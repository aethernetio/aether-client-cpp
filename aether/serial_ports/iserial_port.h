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

#ifndef AETHER_SERIAL_PORTS_ISERIAL_PORT_H_
#define AETHER_SERIAL_PORTS_ISERIAL_PORT_H_

#include <optional>
#include <mutex>

#include "aether/ptr/ptr_view.h"
#include "aether/actions/action.h"
#include "aether/actions/action_ptr.h"
#include "aether/actions/notify_action.h"
#include "aether/actions/action_context.h"
#include "aether/events/event_subscription.h"
#include "aether/events/multi_subscription.h"
#include "aether/types/data_buffer.h"

#include "aether/poller/poller.h"
#include "aether/stream_api/istream.h"
#include "aether/transport/socket_packet_send_action.h"
#include "aether/transport/socket_packet_queue_manager.h"
#include "aether/serial_ports/serial_port_types.h"

namespace ae {
/**
 * \brief Represents serial port interface for device communication.
 */
class ISerialPort : public ByteIStream {
  class ReadAction final : public Action<ReadAction> {
   public:
    ReadAction(ActionContext action_context, ISerialPort& serial_port);

    UpdateStatus Update();

    void Read();
    void Stop();

   private:
    void ReadEvent();

    ISerialPort* serial_port_;
    DataBuffer read_buffer_;
    std::vector<DataBuffer> read_buffers_;
    std::atomic_bool read_event_{false};
    std::atomic_bool error_event_{false};
    std::atomic_bool stop_event_{false};
  };

  class SendAction final : public SocketPacketSendAction {
   public:
    SendAction(ActionContext action_context, ISerialPort& serial_port,
               DataBuffer&& data_buffer);
    SendAction(SendAction&& other) noexcept;

    void Send() override;

   private:
    ISerialPort* serial_port_;
    DataBuffer data_buffer_;
  };

  using ErrorEventAction = NotifyAction;

 protected:
  ISerialPort() = default;

 public:
  explicit ISerialPort(ActionContext action_context, IPoller::ptr const& poller,
                       SerialInit const& serial_init);
  virtual ~ISerialPort() override;

  /**
   * \brief Write amount of data.
   */
  virtual void Write(DataBuffer const& data) = 0;
  /**
   * \brief Read all the data.
   */
  virtual std::optional<DataBuffer> Read() = 0;
  /**
   * \brief Check if the serial port is open.
   */
  virtual bool IsOpen() = 0;

  ActionPtr<StreamWriteAction> Write(DataBuffer&& in_data) override;
  //StreamUpdateEvent::Subscriber stream_update_event() override;
  //StreamInfo stream_info() const override;
  //OutDataEvent::Subscriber out_data_event() override;

 private:
  void Connect();
  void ReadPort();
  void WritePort();
  void ErrorPort();

  void Disconnect();

  ActionContext action_context_;
  PtrView<IPoller> poller_;
  SerialInit serial_init_;

  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;

  OwnActionPtr<SocketPacketQueueManager<SendAction>> send_queue_manager_;
  OwnActionPtr<ErrorEventAction> notify_error_action_;
  OwnActionPtr<ReadAction> read_action_;

  MultiSubscription send_action_error_subs_;
};

} /* namespace ae */

#endif  // AETHER_SERIAL_PORTS_ISERIAL_PORT_H_
