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

#ifndef AETHER_SERIAL_PORT_AT_COMM_SUPPORT_H_
#define AETHER_SERIAL_PORT_AT_COMM_SUPPORT_H_

#include <chrono>
#include <string>

#include "aether/serial_ports/iserial_port.h"
#include "aether/serial_ports/serial_port_types.h"

namespace ae {
class AtCommSupport {
 public:
  AtCommSupport(ISerialPort *serial);

  void SendATCommand(const std::string& command);
  bool WaitForResponse(const std::string& expected, Duration timeout);
  std::string PinToString(const std::uint8_t pin[4]);
 private:
  ISerialPort *serial_;
};

} /* namespace ae */

#endif  // AETHER_LORA_MODULES_EBYTE_E22_400_LM_H_
