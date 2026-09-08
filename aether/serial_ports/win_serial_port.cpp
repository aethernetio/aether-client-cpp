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
  auto lock = std::lock_guard{fd_lock_};
  if (fd_ == INVALID_HANDLE_VALUE) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Port is not open");
    return;
  }

  DWORD bytes_written{0};
  bool success{false};

  overlapped_wr_ = {};
  overlapped_wr_.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  if (overlapped_wr_.hEvent == NULL) {
    AE_TELED_ERROR("Unable to create serial write event {}", GetLastError());
    return;
  }

  auto write_result =
      WriteFile(fd_, data.data(), static_cast<DWORD>(data.size()),
                &bytes_written, &overlapped_wr_);
  if (write_result != FALSE) {
    success = true;
  } else {
    auto error = GetLastError();
    if (error != ERROR_IO_PENDING) {
      AE_TELED_ERROR("WriteFile error {}", error);
    } else {
      auto wait_result = WaitForSingleObject(overlapped_wr_.hEvent, INFINITE);
      if (wait_result == WAIT_OBJECT_0) {
        success = GetOverlappedResult(fd_, &overlapped_wr_, &bytes_written,
                                      FALSE) != FALSE;
        if (!success) {
          AE_TELED_ERROR("GetOverlappedResult error {}", GetLastError());
        }
      } else {
        AE_TELED_ERROR("Wait for serial write error {}", GetLastError());
      }
    }
  }

  CloseHandle(overlapped_wr_.hEvent);
  overlapped_wr_.hEvent = nullptr;

  if (!success) {
    AE_TELED_ERROR("Write to COM port failed");
  } else if (bytes_written > 0) {
    AE_TELED_DEBUG("Serial data write {} bytes", bytes_written);
  }
}

WinSerialPort::DataReadEvent::Subscriber WinSerialPort::read_event() {
  return EventSubscriber{read_event_};
}

bool WinSerialPort::IsOpen() { return fd_ != INVALID_HANDLE_VALUE; }

void WinSerialPort::PollEvent(LPOVERLAPPED overlapped) {
  if (overlapped == &overlapped_rd_) {
    HandleRead();
  }
}

void WinSerialPort::RequestRead() {
  auto lock = std::lock_guard{fd_lock_};
  if (fd_ == INVALID_HANDLE_VALUE) {
    return;
  }

  DWORD bytes_read{0};
  auto read_result =
      ::ReadFile(fd_, read_buffer_.data(),
                 static_cast<DWORD>(read_buffer_.size()), &bytes_read,
                 &overlapped_rd_);
  if (read_result == FALSE) {
    auto error = GetLastError();
    if (error != ERROR_IO_PENDING) {
      AE_TELED_ERROR("Read err {}", error);
      return;
    }
  }
}

void WinSerialPort::HandleRead() {
  bool schedule_emit{false};
  {
    auto lock = std::scoped_lock{fd_lock_};
    if (fd_ == INVALID_HANDLE_VALUE) {
      return;
    }

    DWORD bytes_read{0};
    if (!::GetOverlappedResult(fd_, &overlapped_rd_, &bytes_read, FALSE)) {
      auto error = GetLastError();
      if (error != ERROR_OPERATION_ABORTED) {
        AE_TELED_ERROR("GetOverlappedResult err {}", error);
      }
      return;
    }

    if (bytes_read > 0) {
      DataBuffer data(static_cast<std::size_t>(bytes_read));
      std::copy(read_buffer_.begin(), read_buffer_.begin() + bytes_read,
                data.begin());
      AE_TELED_DEBUG("Serial data read {} bytes", data.size());

      buffers_.emplace_back(std::move(data));
      schedule_emit = !read_flag_.exchange(true);
    }
  }

  if (schedule_emit) {
    scheduler_sub_ =
        ae_context_.scheduler().Task([this]() noexcept { EmitData(); });
  }

  RequestRead();
}

void WinSerialPort::EmitData() {
  std::list<DataBuffer> buffers;
  {
    auto lock = std::scoped_lock{fd_lock_};
    buffers.swap(buffers_);
    read_flag_ = false;
  }

  for (auto const& b : buffers) {
    read_event_.Emit(b);
  }
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
  scheduler_sub_.Reset();

  auto fd = fd_;
  if (fd == INVALID_HANDLE_VALUE) {
    return;
  }

  poller_->Remove({fd});

  auto lock = std::lock_guard{fd_lock_};
  if (fd_ == fd) {
    CancelIoEx(fd_, nullptr);
    CloseHandle(fd_);
    fd_ = INVALID_HANDLE_VALUE;
  }
}

}  // namespace ae

#endif
