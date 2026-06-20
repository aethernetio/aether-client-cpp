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

#  include "aether-miscpp/misc/defer.h"
#  include "aether/serial_ports/serial_ports_tele.h"

namespace ae {

WinSerialPort::WinSerialPort(AeContext const& ae_context,
                             SerialInit serial_init, IPoller::ptr const& poller)
    : ae_context_{ae_context},
      serial_init_{std::move(serial_init)},
      poller_{std::static_pointer_cast<IoCpPoller>(poller->Native())},
      fd_{OpenPort(serial_init_)},
      read_buffer_(kReadBufSize) {
  if (fd_ != INVALID_HANDLE_VALUE) {
    poller_->Add({fd_}, MethodPtr<&WinSerialPort::PollEvent>{this});
    RequestRead();
  }
}

WinSerialPort::~WinSerialPort() { Close(); }

void WinSerialPort::Write(std::span<std::uint8_t const> data) {
  DWORD dwWrite, dwErr, dwRes;
  BOOL fSuccess{FALSE};

  overlapped_wr_.hEvent = CreateEventA(NULL, true, true, NULL);
  if (overlapped_wr_.hEvent == NULL) {
    dwErr = GetLastError();
    AE_TELED_DEBUG("Error write to serial port {}", dwErr);
    return;
  }

  fSuccess = WriteFile(fd_, data.data(), static_cast<DWORD>(data.size()),
                       &dwWrite, &overlapped_wr_);
  dwErr = GetLastError();
  if (!fSuccess) {
    if (dwErr != ERROR_IO_PENDING) {
      AE_TELED_ERROR("WriteFile error {}", dwErr);
    } else {
      dwRes = WaitForSingleObject(overlapped_wr_.hEvent, INFINITE);
      dwErr = GetLastError();
      switch (dwRes) {
        case WAIT_OBJECT_0:
          if (!GetOverlappedResult(fd_, &overlapped_wr_, &dwWrite, FALSE)) {
            dwErr = GetLastError();
            AE_TELED_ERROR("GetOverlappedResult error {}", dwErr);
          } else {
            fSuccess = FALSE;
          }
          break;
        default:
          fSuccess = TRUE;
          break;
      }
    }
  } else {
    fSuccess = FALSE;
  }

  if (overlapped_wr_.hEvent != NULL) CloseHandle(overlapped_wr_.hEvent);

  if (fSuccess == TRUE) {
    AE_TELED_ERROR("Write to com port filed!");
  } else {
    if (dwWrite > 0) {
      AE_TELED_DEBUG("Serial data write {} bytes", dwWrite);
    }
  }
}

WinSerialPort::DataReadEvent::Subscriber WinSerialPort::read_event() {
  return EventSubscriber{read_event_};
}

bool WinSerialPort::IsOpen() { return fd_ != INVALID_HANDLE_VALUE; }

void WinSerialPort::PollEvent(LPOVERLAPPED) { HandleRead(); }

void WinSerialPort::RequestRead() {
  DWORD dwErr, dwRead;
  BOOL fRes;

  if (fd_ == INVALID_HANDLE_VALUE) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Port is not open");
    return;
  }

  auto lock = std::lock_guard{fd_lock_};

  fRes = ::ReadFile(fd_, read_buffer_.data(),
                    static_cast<DWORD>(read_buffer_.size()), &dwRead,
                    &overlapped_rd_);
  if (!fRes) {
    dwErr = GetLastError();
    if (dwErr != ERROR_IO_PENDING) {
      AE_TELED_ERROR("Read err {}", dwErr);
      return;
    }
  }
}

void WinSerialPort::HandleRead() {
  DWORD dwErr;
  DWORD dwRead;

  auto lock = std::scoped_lock{fd_lock_};

  if (!::GetOverlappedResult(fd_, &overlapped_rd_, &dwRead, FALSE)) {
    dwErr = GetLastError();
    AE_TELED_ERROR("GetOverlappedResult err {}", dwErr);
    return;
  }

  if (dwRead > 0) {
    DataBuffer data(static_cast<std::size_t>(dwRead));
    std::copy(read_buffer_.begin(), read_buffer_.begin() + dwRead,
              data.begin());
    AE_TELED_DEBUG("Serial data read {} bytes", data.size());

    buffers_.emplace_back(std::move(data));

    if (!read_flag_.exchange(true)) {
      scheduler_sub_ = ae_context_.scheduler().Task([this]() noexcept {
        auto lock = std::scoped_lock{fd_lock_};
        EmitData();
        read_flag_ = false;
      });
    }
  }
  RequestRead();
}

void WinSerialPort::EmitData() {
  for (auto const& b : buffers_) {
    read_event_.Emit(b);
  }
  buffers_.clear();
}

void* WinSerialPort::OpenPort(SerialInit const& serial_init) {
  void* fd;
  std::string full_name = "\\\\.\\" + serial_init.port_name;

  fd = CreateFileA(full_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                   OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

  if (fd == INVALID_HANDLE_VALUE) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Failed to open port: {}",
                  GetLastError());
    return INVALID_HANDLE_VALUE;
  }

  SetCommMask(fd, EV_RXCHAR);

  auto close_on_exit = ae_defer_at[&] { CloseHandle(fd); };

  if (!SetOptions(fd, serial_init)) {
    return INVALID_HANDLE_VALUE;
  }

  close_on_exit.Reset();

  return fd;
}

bool WinSerialPort::SetOptions(void* fd, SerialInit const& serial_init) {
  DCB dcb{};
  if (!GetCommState(fd, &dcb)) {
    AE_TELE_ERROR(kAdapterSerialPortState, "Failed to get port state");
    return false;
  }

  dcb.BaudRate = static_cast<DWORD>(serial_init.baud_rate);
  dcb.ByteSize = static_cast<BYTE>(serial_init.byte_size);
  dcb.StopBits = static_cast<BYTE>(serial_init.stop_bits);
  dcb.Parity = static_cast<BYTE>(serial_init.parity);
  dcb.fDtrControl = DTR_CONTROL_ENABLE;

  if (!SetCommState(fd, &dcb)) {
    AE_TELE_ERROR(kAdapterSerialConfigurePort, "Failed to configure port");
    return false;
  }

  COMMTIMEOUTS timeouts = {};
  timeouts.ReadIntervalTimeout = 50;
  timeouts.ReadTotalTimeoutConstant = 50;
  timeouts.ReadTotalTimeoutMultiplier = 10;
  timeouts.WriteTotalTimeoutConstant = 50;
  timeouts.WriteTotalTimeoutMultiplier = 10;

  if (!SetCommTimeouts(fd, &timeouts)) {
    AE_TELED_ERROR("Failed to set timeouts");
    return false;
  }

  return true;
}

void WinSerialPort::Close() {
  if (fd_ != INVALID_HANDLE_VALUE) {
    CloseHandle(fd_);
    fd_ = INVALID_HANDLE_VALUE;
  }
}

}  // namespace ae

#endif
