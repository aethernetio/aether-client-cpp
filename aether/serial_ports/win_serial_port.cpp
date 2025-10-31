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

#  include "aether/misc/defer.h"
#  include "aether/serial_ports/serial_ports_tele.h"

namespace ae {

WinSerialPort::ReadAction::ReadAction(ActionContext action_context,
                                      WinSerialPort &serial_port)
    : Action{action_context},
      serial_port_{&serial_port},
      read_event_{},
      read_buffer_(kReadBufSize),
      overlapped_rd_{} {
  if (serial_port_->fd_ == INVALID_HANDLE_VALUE) {
    return;
  }
  auto poller = serial_port_->poller_.Lock();
  assert(poller);
  poll_sub_ =
      poller->Add({serial_port_->fd_})
          .Subscribe(
              *this,
              MethodPtr<&WinSerialPort::ReadAction::ReadAction::PollEvent>{});

  overlapped_rd_.event_type = EventType::kRead;
  RequestRead();
}

UpdateStatus WinSerialPort::ReadAction::Update() {
  if (read_event_) {
    for (auto const &b : buffers_) {
      serial_port_->read_event_.Emit(b);
    }
    buffers_.clear();
    read_event_ = false;
  }
  return {};
}

void WinSerialPort::ReadAction::PollEvent(PollerEvent event) {
  // if (static_cast<HANDLE>(event.descriptor) != serial_port_->fd_)
  if (event.descriptor != DescriptorType{serial_port_->fd_}) {
    return;
  }
  switch (event.event_type) {
    case EventType::kRead:
      ReadData();
      break;
    default:
      break;
  }
}

void WinSerialPort::ReadAction::ReadData() {
  HandleRead();
  RequestRead();
}

void WinSerialPort::ReadAction::RequestRead() {
  DWORD dwErr, dwRead, fRes;
  OVERLAPPED *overlapped_rd = reinterpret_cast<OVERLAPPED *>(&overlapped_rd_);

  if (serial_port_->fd_ == INVALID_HANDLE_VALUE) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Port is not open");
    return;
  }

  auto lock = std::lock_guard{serial_port_->fd_lock_};

  // Issue read operation.
  fRes = ::ReadFile(serial_port_->fd_, read_buffer_.data(),
                    static_cast<DWORD>(read_buffer_.size()), &dwRead,
                    overlapped_rd);
  if (!fRes) {
    dwErr = GetLastError();
    // err should be ERROR_IO_PENDING
    if (dwErr != ERROR_IO_PENDING) {
      AE_TELED_ERROR("Read err {}", dwErr);
      return;
    }
  }

  return;
}

void WinSerialPort::ReadAction::HandleRead() {
  DWORD dwErr;
  DWORD dwRead;
  OVERLAPPED *overlapped_rd = reinterpret_cast<OVERLAPPED *>(&overlapped_rd_);

  if (!::GetOverlappedResult(serial_port_->fd_, overlapped_rd, &dwRead,
                             FALSE)) {
    dwErr = GetLastError();
    AE_TELED_ERROR("GetOverlappedResult err {}", dwErr);
    return;
  }

  if (dwRead > 0) {
    DataBuffer data;
    data.resize(dwRead);
    std::copy(read_buffer_.begin(), read_buffer_.begin() + dwRead,
              data.begin());
    AE_TELED_DEBUG("Serial data read {} bytes: {}", data.size(), data);
    buffers_.emplace_back(std::move(data));
    read_event_ = true;
    Action::Trigger();
  }
}

WinSerialPort::WinSerialPort(ActionContext action_context,
                             SerialInit serial_init, IPoller::ptr const &poller)
    : action_context_{action_context},
      serial_init_{std::move(serial_init)},
      poller_{poller},
      fd_{OpenPort(serial_init_)},
      read_action_{action_context_, *this} {}

WinSerialPort::~WinSerialPort() { Close(); }

void WinSerialPort::Write(DataBuffer const &data) {
  DWORD dwWrite, dwErr, dwRes;
  BOOL fSuccess{FALSE};

  overlapped_wr_.hEvent = CreateEventA(NULL, true, true, NULL);
  if (overlapped_wr_.hEvent == NULL) {
    dwErr = GetLastError();
    // DO smth...
    AE_TELED_DEBUG("Error write to serial port {}", dwErr);

    return;
  }

  fSuccess = WriteFile(fd_, data.data(), static_cast<DWORD>(data.size()),
                       &dwWrite, &overlapped_wr_);
  dwErr = GetLastError();
  if (!fSuccess) {
    if (dwErr != ERROR_IO_PENDING) {  // write not delayed?
      // Error in communications; report it.
      // DO smth...
    } else {
      dwRes = WaitForSingleObject(overlapped_wr_.hEvent, INFINITE);
      dwErr = GetLastError();
      switch (dwRes) {
        // Read completed.
        case WAIT_OBJECT_0:
          if (!GetOverlappedResult(fd_, &overlapped_wr_, &dwWrite, FALSE)) {
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

WinSerialPort::DataReadEvent::Subscriber WinSerialPort::read_event() {
  return EventSubscriber{read_event_};
}

bool WinSerialPort::IsOpen() { return fd_ != INVALID_HANDLE_VALUE; }

void *WinSerialPort::OpenPort(SerialInit const &serial_init) {
  /* open the port */
  void *fd;
  std::string full_name = "\\\\.\\" + serial_init.port_name;

  fd = CreateFileA(full_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                   OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

  if (fd == INVALID_HANDLE_VALUE) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Failed to open port: {}",
                  GetLastError());
    return INVALID_HANDLE_VALUE;
  }

  SetCommMask(fd, EV_RXCHAR);

  auto close_on_exit = defer_at[&] { CloseHandle(fd); };

  if (!SetOptions(fd, serial_init)) {
    return INVALID_HANDLE_VALUE;
  }

  close_on_exit.Reset();

  return fd;
}

bool WinSerialPort::SetOptions(void *fd, SerialInit const &serial_init) {
  // void WINSerialPort::ConfigurePort(std::uint32_t baud_rate) {
  DCB dcb{};
  if (!GetCommState(fd, &dcb)) {
    AE_TELE_ERROR(kAdapterSerialPortState, "Failed to get port state");
    return false;
  }

  dcb.BaudRate = static_cast<DWORD>(serial_init.baud_rate);
  dcb.ByteSize = static_cast<BYTE>(serial_init.byte_size);
  // dcb.StopBits = ONESTOPBIT;
  // dcb.Parity = NOPARITY;
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

} /* namespace ae */

#endif  // WIN_SERIAL_PORT_ENABLED
