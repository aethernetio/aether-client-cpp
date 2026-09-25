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
#  define ESP32_SERIAL_PORT_ENABLED 1
#  include <cstdint>
#  include <span>
#  include <vector>
#  include "aether/ae_context.h"
#  include "aether/serial_ports/iserial_port.h"
#  include "aether/serial_ports/serial_port_types.h"
#  include "driver/uart.h"
namespace ae {
// All calls, including destruction, belong to the AeContext scheduler thread.
class Esp32SerialPort final : public ISerialPort {
 public:
  explicit Esp32SerialPort(AeContext const& context, SerialInit const& init);
  ~Esp32SerialPort() override;
  void Write(std::span<std::uint8_t const> data) override;
  DataReadEvent::Subscriber read_event() override;
  bool IsOpen() override;
  void Close() override;

 private:
  static uart_port_t OpenPort(SerialInit const& init, QueueHandle_t& events);
  void Schedule();
  void Poll();
  void FlushWrite();
  AeContext context_;
  uart_port_t uart_num_{UART_NUM_MAX};
  QueueHandle_t events_{};
  std::vector<std::uint8_t> pending_write_;
  DataReadEvent read_event_;
  TaskSubscription task_;
};
}  // namespace ae
#endif
#endif  // AETHER_SERIAL_PORTS_ESP32_SERIAL_PORT_H_
