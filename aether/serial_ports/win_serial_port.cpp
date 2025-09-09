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

#include "aether/serial_ports/win_serial_port.h"

#if WIN_SERIAL_PORT_ENABLED == 1

#  include "aether/serial_ports/serial_ports_tele.h"

#  include <Windows.h>

namespace ae {
WINSerialPort::WINSerialPort(SerialInit const& serial_init)
    : h_port_{INVALID_HANDLE_VALUE} {
  Open(serial_init.port_name,
       static_cast<std::uint32_t>(serial_init.baud_rate));
}

WINSerialPort::~WINSerialPort() { Close(); }

void WINSerialPort::Write(DataBuffer const& data) {
  if (h_port_ == INVALID_HANDLE_VALUE) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Port is not open");
    return;
  }

  DWORD bytes_written;
  if (!WriteFile(h_port_, data.data(), static_cast<DWORD>(data.size()),
                 &bytes_written, NULL)) {
    AE_TELE_ERROR(kAdapterSerialWriteFailed, "Write failed: {}",
                  GetLastError());
    return;
  }

  if (bytes_written != data.size()) {
    AE_TELE_ERROR(kAdapterSerialPartialData, "Partial write occurred");
    return;
  }

  // For debug
  AE_TELED_DEBUG("Serial data write {} bytes: {}", bytes_written,
                 std::vector(data.begin(), data.end()));
}

std::optional<DataBuffer> WINSerialPort::Read() {
  if (h_port_ == INVALID_HANDLE_VALUE) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Port is not open");

    return std::nullopt;
  }

  DataBuffer buffer(4096);
  DWORD bytes_read = 0;

  if (!ReadFile(h_port_, buffer.data(), static_cast<DWORD>(buffer.size()),
                &bytes_read, NULL)) {
    if (GetLastError() != ERROR_IO_PENDING) {
      return std::nullopt;
    }
  }

  if (bytes_read > 0) {
    buffer.resize(bytes_read);
    // For debug
    AE_TELED_DEBUG("Serial data read {} bytes: {}", bytes_read,
                   std::vector(buffer.begin(), buffer.end()));
    return buffer;
  }
  return std::nullopt;
}

bool WINSerialPort::IsOpen() { return h_port_ != INVALID_HANDLE_VALUE; }

void WINSerialPort::Open(std::string const& port_name,
                         std::uint32_t baud_rate) {
  std::string full_name = "\\\\.\\" + port_name;
  h_port_ = CreateFileA(full_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  if (h_port_ == INVALID_HANDLE_VALUE) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Failed to open port: {}",
                  GetLastError());
    return;
  }

  ConfigurePort(baud_rate);
  SetupTimeouts();
}

void WINSerialPort::ConfigurePort(std::uint32_t baud_rate) {
  DCB dcb{};
  if (!GetCommState(h_port_, &dcb)) {
    Close();
    AE_TELE_ERROR(kAdapterSerialPortState, "Failed to get port state");
  }

  dcb.BaudRate = baud_rate;
  dcb.ByteSize = 8;
  dcb.StopBits = ONESTOPBIT;
  dcb.Parity = NOPARITY;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;

  if (!SetCommState(h_port_, &dcb)) {
    Close();
    AE_TELE_ERROR(kAdapterSerialConfigurePort, "Failed to configure port");
  }
}

void WINSerialPort::SetupTimeouts() {
  COMMTIMEOUTS timeouts = {};
  timeouts.ReadIntervalTimeout = 50;
  timeouts.ReadTotalTimeoutConstant = 50;
  timeouts.ReadTotalTimeoutMultiplier = 10;
  timeouts.WriteTotalTimeoutConstant = 50;
  timeouts.WriteTotalTimeoutMultiplier = 10;

  if (!SetCommTimeouts(h_port_, &timeouts)) {
    Close();
    AE_TELED_ERROR("Failed to set timeouts");
  }
}

void WINSerialPort::Close() {
  if (h_port_ != INVALID_HANDLE_VALUE) {
    CloseHandle(h_port_);
    h_port_ = INVALID_HANDLE_VALUE;
  }
}

} /* namespace ae */

#endif  // WIN_SERIAL_PORT_ENABLED
