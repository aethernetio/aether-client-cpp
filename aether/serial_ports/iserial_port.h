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

#include "aether/types/data_buffer.h"

namespace ae {
/**
 * \brief Represents serial port interface for device communication.
 */
class ISerialPort {
 public:
  virtual ~ISerialPort() = default;

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
};

} /* namespace ae */

#endif  // AETHER_SERIAL_PORTS_ISERIAL_PORT_H_
