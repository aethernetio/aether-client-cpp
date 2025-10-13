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

#  define READ_BUF_SIZE 4096
#  define READ_TIMEOUT_MSEC 250

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
  DWORD dwWrite, dwErr, dwRes;
  BOOL fSuccess{FALSE};
  OVERLAPPED osReader = {0};

  overlapped_wr_.hEvent = CreateEventA(NULL, true, true, NULL);
  if (overlapped_wr_.hEvent == NULL) {
    dwErr = GetLastError();
    // DO smth...
    AE_TELED_DEBUG("Error write to serial port {}", dwErr);

    return;
  }

  osReader.hEvent = overlapped_wr_.hEvent;

  fSuccess = WriteFile(h_port_, data.data(), static_cast<DWORD>(data.size()),
                       &dwWrite, &osReader);
  dwErr = GetLastError();
  if (!fSuccess) {
    if (dwErr != ERROR_IO_PENDING) {  // write not delayed?
      // Error in communications; report it.
      // DO smth...
    } else {
      dwRes = WaitForSingleObject(osReader.hEvent, INFINITE);
      dwErr = GetLastError();
      switch (dwRes) {
        // Read completed.
        case WAIT_OBJECT_0:
          if (!GetOverlappedResult(h_port_, &osReader, &dwWrite, FALSE)) {
            dwErr = GetLastError();
            // Error in communications; report it.
            // DO smth...
          } else {
            // Write completed successfully.
            fSuccess = FALSE;
          }
          break;
        case WAIT_TIMEOUT:
          // NEVER == INFININE
          fSuccess = TRUE;
          break;
        default:
          // Error in the WaitForSingleObject; abort.
          // This indicates a problem with the OVERLAPPED structure's
          // event handle.
          fSuccess = TRUE;
          break;
      }
    }
  } else {
    // write completed immediately
    fSuccess = FALSE;
  }

  if (overlapped_wr_.hEvent != NULL) CloseHandle(overlapped_wr_.hEvent);

  // For debug
  if (fSuccess == TRUE) {
    AE_TELED_ERROR("Write to com port filed!");
  } else {
    if (dwWrite > 0) {
      AE_TELED_DEBUG("Serial data write {} bytes: {}", dwWrite,
                     std::vector(data.begin(), data.end()));
    }
  }
}

std::optional<DataBuffer> WINSerialPort::Read() {
  DWORD dwErr, dwRead, dwRes, fRes;
  BOOL fSuccess{FALSE};
  OVERLAPPED osReader = {0};
  DataBuffer buffer(READ_BUF_SIZE);

  if (h_port_ == INVALID_HANDLE_VALUE) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Port is not open");

    return std::nullopt;
  }

  overlapped_rd_.hEvent = CreateEventA(NULL, true, true, NULL);
  if (overlapped_rd_.hEvent == NULL) {
    dwErr = GetLastError();
    // DO smth...
    AE_TELED_DEBUG("Error write to serial port {}", dwErr);
  }

  osReader.hEvent = overlapped_rd_.hEvent;

  if (osReader.hEvent == NULL) {
    // error creating overlapped event handle
    return {};
  }

  // Issue read operation.
  fRes = ::ReadFile(h_port_, buffer.data(), READ_BUF_SIZE, &dwRead, &osReader);

  dwErr = GetLastError();
  if (dwErr != ERROR_IO_PENDING) {
    AE_TELED_ERROR("Read err {}", dwErr);
  }

  if (!fRes) {
    // if (::GetLastError() != ERROR_IO_PENDING) {
    if (false) {
      // WriteFile failed, but isn't delayed. Report error and abort.
      fSuccess = FALSE;
    } else {
      dwRes = ::WaitForSingleObject(osReader.hEvent, READ_TIMEOUT_MSEC);

      switch (dwRes) {
        // Read completed.
        case WAIT_OBJECT_0:

          if (!::GetOverlappedResult(h_port_, &osReader, &dwRead, FALSE)) {
            // Error in communications; report it.
            // throw new exception("Error in communications");

          } else {
            // Read completed successfully.
            fSuccess = TRUE;
          }

          break;

        case WAIT_TIMEOUT:

          // Operation isn't complete yet. fWaitingOnRead flag isn't
          // changed since I'll loop back around, and I don't want
          // to issue another read until the first one finishes.
          //
          // This is a good time to do some background work.
          fSuccess = FALSE;
          break;

        default:
          // Error in the WaitForSingleObject; abort.
          // This indicates a problem with the OVERLAPPED structure's
          // event handle.
          fSuccess = FALSE;
          break;
      }
    }
  }

  if (overlapped_rd_.hEvent != NULL) ::CloseHandle(overlapped_rd_.hEvent);

  // For debug
  if (fSuccess == TRUE) {
    AE_TELED_ERROR("Read from com port filed!");
  } else {
    if (dwRead > 0) {
      AE_TELED_DEBUG("Serial data read {} bytes: {}", dwRead, buffer);
    }
  }

  return buffer;
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

  SetCommMask(h_port_, EV_RXCHAR);
  h_port_ = CreateFileA(full_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                        NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

  if (h_port_ == INVALID_HANDLE_VALUE) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Failed to open port: {}",
                  GetLastError());
    return;
  }

  ConfigurePort(baud_rate);
  SetupTimeouts();

  // Connect();
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
