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

#ifndef AETHER_SERIAL_PORTS_UNIX_SERIAL_PORT_H_
#define AETHER_SERIAL_PORTS_UNIX_SERIAL_PORT_H_

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
    defined(__FreeBSD__)

#  define UNIX_SERIAL_PORT_ENABLED 1

#  include "aether/serial_ports/iserial_port.h"
#  include "aether/serial_ports/serial_port_types.h"

namespace ae {
class UnixSerialPort final : public ISerialPort {
 public:
  explicit UnixSerialPort(SerialInit const& serial_init);
  ~UnixSerialPort() override;

  void Write(DataBuffer const& data) override;

  std::optional<DataBuffer> Read() override;

  bool IsOpen() override;

 private:
  static int OpenPort(SerialInit const& serial_init);
  static bool SetOptions(int fd, SerialInit const& serial_init);

  void Close();

  int fd_;
};
}  // namespace ae

#endif

#endif  // AETHER_SERIAL_PORTS_UNIX_SERIAL_PORT_H_
