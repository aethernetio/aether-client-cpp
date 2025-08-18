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

namespace ae {
class ESP32SerialPort : public ISerialPort {
 public:
  ESP32SerialPort(SerialInit const& serial_init);
  ~ESP32SerialPort() override;

  void Write(DataBuffer const& data) override;
  std::optional<DataBuffer> Read() override;
  bool IsOpen() override;

 private:
  void* h_port_;

  void Open(std::string const& port_name, std::uint32_t baud_rate);
  void ConfigurePort(std::uint32_t baud_rate);
  void SetupTimeouts();
  void Close();
};
} /* namespace ae */

#endif  // ESP_PLATFORM
#endif  // AETHER_SERIAL_PORTS_ESP32_SERIAL_PORT_H_
