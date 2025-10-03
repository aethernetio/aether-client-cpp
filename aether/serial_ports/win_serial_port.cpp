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

namespace ae {
WINSerialPort::WINSerialPort(ActionContext action_context,
                             IPoller::ptr const& poller,
                             SerialInit const& serial_init)
    : ISerialPort{std::move(action_context), std::move(poller),
                  std::move(serial_init)},
      action_context_{std::move(action_context)},
      poller_{std::move(poller)},
      h_port_{INVALID_HANDLE_VALUE} {
  Open(serial_init.port_name,
       static_cast<std::uint32_t>(serial_init.baud_rate));
}

WINSerialPort::~WINSerialPort() { Close(); }

void WINSerialPort::Write(DataBuffer const& data) {
  DWORD temp;

  if (h_port_ == INVALID_HANDLE_VALUE) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Port is not open");
    return;
  }

  if (!WriteFile(h_port_, data.data(), static_cast<DWORD>(data.size()), &temp,
                 &overlapped_wr_)) {
    AE_TELE_ERROR(kAdapterSerialWriteFailed, "Write failed: {}",
                  GetLastError());
    return;
  }

  signal_ = WaitForSingleObject(overlapped_wr_.hEvent, INFINITE);
  if ((signal_ == WAIT_OBJECT_0) && (GetOverlappedResult(h_port_, &overlapped_wr_, &temp, true))) {
    AE_TELED_DEBUG("Write ok!");
  } else {
    AE_TELED_DEBUG("Write not ok!");
  }
  CloseHandle(overlapped_wr_.hEvent);

  if (temp != data.size()) {
    AE_TELE_ERROR(kAdapterSerialPartialData, "Partial write occurred");
    return;
  }

  // For debug
  AE_TELED_DEBUG("Serial data write {} bytes: {}", temp,
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

void WINSerialPort::Connect() {
  AE_TELE_INFO(kSerialTransportConnect, "Serial port connect");

  auto poller_ptr = poller_.Lock();
  assert(poller_ptr);
  port_event_sub_ =
      poller_ptr->Add(static_cast<DescriptorType>(h_port_))
          .Subscribe([this](PollerEvent event) {
            if (event.descriptor != static_cast<DescriptorType>(h_port_)) {
              return;
            }
            switch (event.event_type) {
              case EventType::kRead:
                ReadPort();
                break;
              case EventType::kWrite:
                WritePort();
                break;
              case EventType::kError:
                ErrorPort();
                break;
            }
          });
}

void WINSerialPort::ReadPort() { AE_TELED_DEBUG("Read port"); }

void WINSerialPort::WritePort() { AE_TELED_DEBUG("Write port"); }

void WINSerialPort::ErrorPort() { AE_TELED_DEBUG("Error port"); }

void WINSerialPort::Disconnect() {}

void WINSerialPort::Open(std::string const& port_name,
                         std::uint32_t baud_rate) {
  std::string full_name = "\\\\.\\" + port_name;
  overlapped_rd_.hEvent = CreateEventA(NULL, true, true, NULL);
  overlapped_wr_.hEvent = CreateEventA(NULL, true, true, NULL); 
  SetCommMask(h_port_, EV_RXCHAR);
  h_port_ = CreateFileA(full_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
      /* FILE_ATTRIBUTE_NORMAL | */ FILE_FLAG_OVERLAPPED,
                       NULL);

  if (h_port_ == INVALID_HANDLE_VALUE) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Failed to open port: {}",
                  GetLastError());
    return;
  }

  ConfigurePort(baud_rate);
  SetupTimeouts();
  
  //Connect();
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
    
    Disconnect();
  }
}

} /* namespace ae */

#endif  // WIN_SERIAL_PORT_ENABLED
