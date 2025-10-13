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

#  include <Windows.h>

#  define WIN_SERIAL_PORT_ENABLED 1

namespace ae {
class WINSerialPort final : public ISerialPort {
 protected:
  WINSerialPort() = default;
  
 public:
  WINSerialPort(ActionContext action_context, IPoller::ptr const& poller,
                SerialInit const& serial_init);
  ~WINSerialPort() override;

  void Write(DataBuffer const& data) override;
  std::optional<DataBuffer> Read() override;
  bool IsOpen() override;

 private:
  OVERLAPPED overlapped_rd_;
  OVERLAPPED overlapped_wr_;
  DWORD signal_;
  ActionContext action_context_;
  PtrView<IPoller> poller_;
  void* h_port_;

  void Connect();
  void ReadPort();
  void WritePort();
  void ErrorPort();
  void Disconnect();
  
  void Open(std::string const& port_name, std::uint32_t baud_rate);
  void ConfigurePort(std::uint32_t baud_rate);
  void SetupTimeouts();
  void Close();
  
  IPoller::OnPollEventSubscriber::Subscription port_event_sub_;
  Subscription port_error_sub_;
  Subscription read_error_sub_;
};
} /* namespace ae */

#endif  // _WIN32
#endif  // AETHER_SERIAL_PORTS_WIN_SERIAL_PORT_H_
