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

#ifndef AETHER_SERIAL_PORTS_ESP32_SERIAL_PORT_H_
#define AETHER_SERIAL_PORTS_ESP32_SERIAL_PORT_H_

#if defined(ESP_PLATFORM)

#  include <vector>
#  include <string>
#  include <optional>

#  include "driver/uart.h"

#  include "aether/ptr/ptr_view.h"
#  include "aether/actions/action.h"
#  include "aether/actions/action_ptr.h"
#  include "aether/actions/action_context.h"

#  include "aether/poller/poller.h"
#  include "aether/serial_ports/iserial_port.h"
#  include "aether/serial_ports/serial_port_types.h"

#  define ESP32_SERIAL_PORT_ENABLED 1

namespace ae {
class Esp32SerialPort : public ISerialPort {
  class ReadAction final : public Action<ReadAction> {
   public:
    ReadAction(ActionContext action_context, Esp32SerialPort& serial_port);

    UpdateStatus Update();

   private:
    void PollEvent(PollerEvent event);
    void ReadData();

    Esp32SerialPort* serial_port_;
    Subscription poll_sub_;
    std::list<DataBuffer> buffers_;
    std::atomic_bool read_event_;
  };

 public:
  Esp32SerialPort(ActionContext action_context, SerialInit serial_init,
                  IPoller::ptr const& poller);
  ~Esp32SerialPort() override;

  void Write(DataBuffer const& data) override;

  DataReadEvent::Subscriber read_event() override;

  bool IsOpen() override;

 private:
  uart_port_t OpenPort(SerialInit const& serial_init);
  bool SetOptions(uart_port_t uart_num, SerialInit const& serial_init);
  esp_err_t SetupTimeouts();
  bool GetUartNumber(const std::string& port_name, uart_port_t* out_uart_num);

  void Close();

  ActionContext action_context_;
  SerialInit serial_init_;
  PtrView<IPoller> poller_;
  uart_port_t uart_num_;

  DataReadEvent read_event_;

  ActionPtr<ReadAction> read_action_;
};
} /* namespace ae */

#endif  // ESP_PLATFORM
#endif  // AETHER_SERIAL_PORTS_ESP32_SERIAL_PORT_H_
