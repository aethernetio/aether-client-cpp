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
#  include "aether/serial_ports/serial_port_types.h"

#  define WIN_SERIAL_PORT_ENABLED 1

namespace ae {

// A thread for reading a sequence of bytes from a COM port into a buffer
class ReadThread : public TThread
{
private:
void __fastcall Output(); // Output function
protected:
void __fastcall Execute(); // Main thread function
public:
__fastcall ReadThread(bool CreateSuspended); // Thread constructor
};

// A stream for writing a sequence of bytes from a buffer to a COM port
class WriteThread : public TThread
{
private:
void __fastcall Output(); // Output function
protected:
void __fastcall Execute(); // Main thread function
public:
__fastcall WriteThread(bool CreateSuspended); // Thread constructor
};

class WINThreadSerialPort final : public ISerialPort {
 public:
  WINThreadSerialPort(SerialInit const& serial_init);
  ~WINThreadSerialPort() override;

  void Write(DataBuffer const& data) override;
  std::optional<DataBuffer> Read() override;
  bool IsOpen() override;

 private:
  void* h_port_;
  ReadThread *reader;
  WriteThread *writer;

  void Open(std::string const& port_name, std::uint32_t baud_rate);
  void ConfigurePort(std::uint32_t baud_rate);
  void SetupTimeouts();
  void Close();
};
} /* namespace ae */

#endif  // _WIN32
#endif  // AETHER_SERIAL_PORTS_WIN_SERIAL_PORT_H_