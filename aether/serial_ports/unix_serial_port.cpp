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

#include "aether/serial_ports/unix_serial_port.h"

#if defined UNIX_SERIAL_PORT_ENABLED

#  include <fcntl.h>
#  include <unistd.h>
#  include <termios.h>

#  include "aether/misc/defer.h"
#  include "aether/serial_ports/serial_ports_tele.h"

namespace ae {
UnixSerialPort::UnixSerialPort(AeContext const& ae_context,
                               SerialInit serial_init,
                               IPoller::ptr const& poller)
    : ae_context_{ae_context},
      serial_init_{std::move(serial_init)},
      poller_fd_{OpenPort(serial_init_), poller->Native(),
                 MethodPtr<&UnixSerialPort::PolleEvent>{this}} {
  poller_fd_.Events(EventType::kRead | EventType::kError);
}

UnixSerialPort::~UnixSerialPort() {
  auto fd = poller_fd_.Remove();
  if (*fd != kInvalidDescriptor) {
    close(*fd);
  }
}

void UnixSerialPort::Write(std::span<std::uint8_t const> data) {
  auto bytes_written = write(*poller_fd_.fd(), data.data(), data.size());
  if (bytes_written < 0) {
    AE_TELE_ERROR(kAdapterSerialWriteFailed, "Write serial port error {}",
                  strerror(errno));
  }

  if (bytes_written != data.size()) {
    AE_TELE_ERROR(kAdapterSerialPartialData, "Partial write occurred");
    return;
  }
}

UnixSerialPort::DataReadEvent::Subscriber UnixSerialPort::read_event() {
  return EventSubscriber{read_event_};
}

bool UnixSerialPort::IsOpen() { return *poller_fd_.fd() != kInvalidDescriptor; }

int UnixSerialPort::OpenPort(SerialInit const& serial_init) {
  /* open the port */
  int fd = open(serial_init.port_name.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
  if (fd < 0) {
    AE_TELED_ERROR("Open serial port at {} error {}", serial_init.port_name,
                   strerror(errno));
    return kInvalidDescriptor;
  }
  auto close_on_exit = ae_defer_at[&] { close(fd); };

  if (!SetOptions(fd, serial_init)) {
    return kInvalidDescriptor;
  }
  close_on_exit.Reset();
  return fd;
}

bool UnixSerialPort::SetOptions(int fd, SerialInit const& serial_init) {
  struct termios options;
  fcntl(fd, F_SETFL, 0);
  /* get the current options */
  if (tcgetattr(fd, &options) < 0) {
    AE_TELE_ERROR(kAdapterSerialPortState, "Get serial port options error {}",
                  strerror(errno));
    return false;
  }

  /* set raw input, 0.5 second timeout */
  options.c_cflag |= (CLOCAL | CREAD);
  options.c_lflag &=
      static_cast<decltype(options.c_lflag)>(~(ICANON | ECHO | ECHOE | ISIG));
  options.c_oflag &= static_cast<decltype(options.c_oflag)>(~OPOST);
  options.c_cc[VMIN] = 0;
  options.c_cc[VTIME] = 5;  // 0.5 seconds

  // set baud rate
  auto const baud = static_cast<speed_t>(serial_init.baud_rate);
  cfsetispeed(&options, baud);
  cfsetospeed(&options, baud);

  if (tcsetattr(fd, TCSANOW, &options) < 0) {
    AE_TELE_ERROR(kAdapterSerialConfigurePort,
                  "Set serial port options error {}", strerror(errno));
    return false;
  }

  return true;
}

void UnixSerialPort::PolleEvent(DescriptorType fd, EventType event) {
  auto event_type = event & EventType::kRead;
  switch (event_type) {
    case EventType::kRead:
      ReadData(fd);
      break;
    default:
      break;
  }
}

void UnixSerialPort::ReadData(DescriptorType fd) {
  static std::uint8_t buffer[1024];  // NOLINT(*avoid-c-arrays, *magic-numbers)
  ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
  if (bytes_read < 0) {
    AE_TELED_ERROR("Read serial port error {}", strerror(errno));
    return;
  }
  DataBuffer data(static_cast<std::size_t>(bytes_read));
  std::copy(buffer, buffer + bytes_read, data.begin());

  auto lock = std::scoped_lock{buffers_lock_};
  buffers_.emplace_back(std::move(data));

  if (!read_flag_.exchange(true)) {
    scheduler_sub_ = ae_context_.scheduler().Task([this]() noexcept {
      auto lock = std::scoped_lock{buffers_lock_};
      EmitData();
      read_flag_ = false;
    });
  }
}

void UnixSerialPort::EmitData() {
  for (auto const& b : buffers_) {
    read_event_.Emit(b);
  }
  buffers_.clear();
}
}  // namespace ae

#endif
