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

#ifndef AETHER_ADAPTERS_MODEMS_ISERIAL_PORT_H_
#define AETHER_ADAPTERS_MODEMS_ISERIAL_PORT_H_

#include <vector>
#include <optional>

#include "aether/aether.h"

namespace ae {

enum class kModemBaudRate : std::uint32_t {
  kBaudRate0 = 0,
  kBaudRate300 = 300,
  kBaudRate600 = 600,
  kBaudRate1200 = 1200,
  kBaudRate2400 = 2400,
  kBaudRate4800 = 4800,
  kBaudRate9600 = 9600,
  kBaudRate19200 = 19200,
  kBaudRate38400 = 38400,
  kBaudRate57600 = 57600,
  kBaudRate115200 = 115200,
  kBaudRate230400 = 230400,
  kBaudRate921600 = 921600,
  kBaudRate2000000 = 2000000,
  kBaudRate2900000 = 2900000,
  kBaudRate3000000 = 3000000,
  kBaudRate3200000 = 3200000,
  kBaudRate3684000 = 3684000,
  kBaudRate4000000 = 4000000
};

struct SerialInit {
  AE_REFLECT_MEMBERS(port_name, baud_rate)
  std::string port_name;
  kModemBaudRate baud_rate;
};

using DataBuffer = std::vector<uint8_t>;

class ISerialPort{

  public:
   ISerialPort()  = default;
   virtual ~ISerialPort() = default;
   
   virtual void WriteData(const DataBuffer& data) = 0;
   virtual std::optional<DataBuffer> ReadData() = 0;
   virtual bool GetConnected() = 0;
};

} /* namespace ae */

#endif  // AETHER_ADAPTERS_MODEMS_ISERIAL_PORT_H_
