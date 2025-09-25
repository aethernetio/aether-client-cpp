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

#include "aether/serial_ports/serial_port_factory.h"

// IWYU pragma: begin_keeps
#include "aether/serial_ports/win_serial_port.h"
#include "aether/serial_ports/esp32_serial_port.h"
#include "aether/serial_ports/unix_serial_port.h"
// IWYU pragma: end_keeps

namespace ae {

std::unique_ptr<ISerialPort> SerialPortFactory::CreatePort(
    [[maybe_unused]] SerialInit const& serial_init) {
#if WIN_SERIAL_PORT_ENABLED == 1
  return std::make_unique<WINSerialPort>(serial_init);
#elif ESP32_SERIAL_PORT_ENABLED == 1
  return std::make_unique<ESP32SerialPort>(serial_init);
#elif UNIX_SERIAL_PORT_ENABLED == 1
  return std::make_unique<UnixSerialPort>(serial_init);
#else
  return nullptr;
#endif
}

}  // namespace ae
