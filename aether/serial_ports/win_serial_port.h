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

#ifndef AETHER_SERIAL_PORTS_WIN_SERIAL_PORT_H_
#define AETHER_SERIAL_PORTS_WIN_SERIAL_PORT_H_

#if defined _WIN32

#  include <vector>
#  include <string>
#  include <memory>
#  include <optional>
#  include <stdexcept>

#  include "aether/serial_ports/iserial_port.h"

#  define WIN_SERIAL_PORT_ENABLED 1

namespace ae {
class WINSerialPort final : public ISerialPort {
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
  WINSerialPort() = default;
  
 public:
  /*WINSerialPort(ActionContext action_context, IPoller::ptr const& poller,
                SerialInit const& serial_init);
  ~WINSerialPort() override;

  void Write(DataBuffer const& data) override;
  std::optional<DataBuffer> Read() override;
  bool IsOpen() override;*/

 private:
  ActionContext action_context_;
  PtrView<IPoller> poller_;
  void* h_port_;

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
  
  void Open(std::string const& port_name, std::uint32_t baud_rate);
  void ConfigurePort(std::uint32_t baud_rate);
  void SetupTimeouts();
  void Close();
};
} /* namespace ae */

#endif  // _WIN32
#endif  // AETHER_SERIAL_PORTS_WIN_SERIAL_PORT_H_
