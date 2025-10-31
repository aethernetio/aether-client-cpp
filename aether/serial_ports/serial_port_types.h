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

#ifndef AETHER_SERIAL_PORTS_SERIAL_PORT_TYPES_H_
#define AETHER_SERIAL_PORTS_SERIAL_PORT_TYPES_H_

#include <string>
#include <cstdint>

#include "aether/reflect/reflect.h"

namespace ae {
enum class kBaudRate : std::uint32_t {
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
  kBaudRate128000 = 128000,
  kBaudRate230400 = 230400,
  kBaudRate921600 = 921600,
  kBaudRate2000000 = 2000000,
  kBaudRate2900000 = 2900000,
  kBaudRate3000000 = 3000000,
  kBaudRate3200000 = 3200000,
  kBaudRate3684000 = 3684000,
  kBaudRate4000000 = 4000000
};

enum class kBits : std::uint8_t {
  kFiveBits = 5,
  kSixBits = 6,
  kSevenBits = 7,
  kEigthBits = 8,
  kNineBits = 9
};

enum class kParity : std::uint8_t {
  kNoParity = 0,
  kOddParity = 1,
  kEvenParity = 2,
  kMarkParity = 3,
  kSpaceParity = 4
};

enum class kStopBits : std::uint8_t {
  kOneStopBit = 1,
  kOneAndHalfStopBit = 2,
  kTwoStopBit = 3
};

struct SerialInit {
  AE_REFLECT_MEMBERS(port_name, baud_rate, byte_size, parity, stop_bits,
                     tx_io_num, rx_io_num, rts_io_num, cts_io_num)

  std::string port_name{};
  kBaudRate baud_rate{kBaudRate::kBaudRate115200};
  kBits byte_size{kBits::kEigthBits};
  kParity parity{kParity::kNoParity};
  kStopBits stop_bits{kStopBits::kOneStopBit};
  int tx_io_num{1};
  int rx_io_num{2};
  int rts_io_num{3};
  int cts_io_num{4};
};
}  // namespace ae

#endif  // AETHER_SERIAL_PORTS_SERIAL_PORT_TYPES_H_
