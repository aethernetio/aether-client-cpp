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

#include "aether/adapters/modems/win_serial_port.h"
#include "aether/adapters/adapter_tele.h"

namespace ae {
WINSerialPort::WINSerialPort(const std::string& portName, DWORD baudRate)
    : hPort_(INVALID_HANDLE_VALUE) {
  Open(portName, baudRate);
}

WINSerialPort::~WINSerialPort() { Close(); }

void WINSerialPort::Write(const DataBuffer& data) {
  if (hPort_ == INVALID_HANDLE_VALUE) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Port is not open");
  }

  DWORD bytesWritten;
  if (!WriteFile(hPort_, data.data(), static_cast<DWORD>(data.size()),
                 &bytesWritten, NULL)) {
    AE_TELE_ERROR(kAdapterSerialWriteFiled, "Write failed: {}", std::to_string(GetLastError()));
  }

  if (bytesWritten != data.size()) {
    AE_TELE_ERROR(kAdapterSerialPartialData, "Partial write occurred");
  }
}

std::optional<DataBuffer> WINSerialPort::Read() {
  if (hPort_ == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }

  DataBuffer buffer(1024);
  DWORD bytesRead = 0;

  if (!ReadFile(hPort_, buffer.data(), static_cast<DWORD>(buffer.size()),
                &bytesRead, NULL)) {
    if (GetLastError() != ERROR_IO_PENDING) {
      return std::nullopt;
    }
  }

  if (bytesRead > 0) {
    buffer.resize(bytesRead);
    return buffer;
  }
  return std::nullopt;
}

void WINSerialPort::Open(const std::string& portName, DWORD baudRate) {
  std::string fullName = "\\\\.\\" + portName;
  hPort_ = CreateFileA(fullName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  if (hPort_ == INVALID_HANDLE_VALUE) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Failed to open port: {}",
                             std::to_string(GetLastError()));
  }

  ConfigurePort(baudRate);
  SetupTimeouts();
}

void WINSerialPort::ConfigurePort(DWORD baudRate) {
  DCB dcb = {sizeof(DCB)};
  if (!GetCommState(hPort_, &dcb)) {
    Close();
    AE_TELE_ERROR(kAdapterSerialPortState, "Failed to get port state");
  }

  dcb.BaudRate = baudRate;
  dcb.ByteSize = 8;
  dcb.StopBits = ONESTOPBIT;
  dcb.Parity = NOPARITY;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;

  if (!SetCommState(hPort_, &dcb)) {
    Close();
    AE_TELE_ERROR(kAdapterSerialConfigurePort, "Failed to configure port");
  }
}

void WINSerialPort::SetupTimeouts() {
  COMMTIMEOUTS timeouts = {0};
  timeouts.ReadIntervalTimeout = 50;
  timeouts.ReadTotalTimeoutConstant = 50;
  timeouts.ReadTotalTimeoutMultiplier = 10;
  timeouts.WriteTotalTimeoutConstant = 50;
  timeouts.WriteTotalTimeoutMultiplier = 10;

  if (!SetCommTimeouts(hPort_, &timeouts)) {
    Close();
    AE_TELED_ERROR("Failed to set timeouts");
  }
}

void WINSerialPort::Close() {
  if (hPort_ != INVALID_HANDLE_VALUE) {
    CloseHandle(hPort_);
    hPort_ = INVALID_HANDLE_VALUE;
  }
}

} /* namespace ae */
