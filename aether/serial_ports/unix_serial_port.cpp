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
static constexpr int kInvalidPort = -1;

UnixSerialPort::ReadAction::ReadAction(ActionContext action_context,
                                       UnixSerialPort& serial_port)
    : Action{action_context}, serial_port_{&serial_port}, read_event_{} {
  if (serial_port_->fd_ == kInvalidPort) {
    return;
  }
  serial_port_->poller_->Event(
      serial_port_->fd_, EventType::kRead,
      MethodPtr<&UnixSerialPort::ReadAction::ReadAction::PollEvent>{this});
}

UpdateStatus UnixSerialPort::ReadAction::Update() {
  if (read_event_) {
    auto lock = std::scoped_lock{serial_port_->fd_lock_};
    for (auto const& b : buffers_) {
      serial_port_->read_event_.Emit(b);
    }
    buffers_.clear();
    read_event_ = false;
  }
  return {};
}

void UnixSerialPort::ReadAction::PollEvent(EventType event) {
  auto event_type = event & EventType::kRead;
  switch (event_type) {
    case EventType::kRead:
      ReadData();
      break;
    default:
      break;
  }
}

void UnixSerialPort::ReadAction::ReadData() {
  auto lock = std::scoped_lock{serial_port_->fd_lock_};

  static std::uint8_t buffer[1024];
  ssize_t bytes_read = read(serial_port_->fd_, buffer, sizeof(buffer));
  if (bytes_read < 0) {
    AE_TELED_ERROR("Read serial port error {}", strerror(errno));
    return;
  }
  DataBuffer data(static_cast<std::size_t>(bytes_read));
  std::copy(buffer, buffer + bytes_read, data.begin());
  buffers_.emplace_back(std::move(data));
  read_event_ = true;
  Action::Trigger();
}

UnixSerialPort::UnixSerialPort(ActionContext action_context,
                               SerialInit serial_init,
                               IPoller::ptr const& poller)
    : action_context_{action_context},
      serial_init_{std::move(serial_init)},
      poller_{static_cast<UnixPollerImpl*>(poller->Native())},
      fd_{OpenPort(serial_init_)},
      read_action_{action_context_, *this} {}

UnixSerialPort::~UnixSerialPort() { Close(); }

void UnixSerialPort::Write(DataBuffer const& data) {
  auto bytes_written = write(fd_, data.data(), data.size());
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

bool UnixSerialPort::IsOpen() { return fd_ != kInvalidPort; }

int UnixSerialPort::OpenPort(SerialInit const& serial_init) {
  /* open the port */
  int fd = open(serial_init.port_name.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
  if (fd < 0) {
    AE_TELED_ERROR("Open serial port at {} error {}", serial_init.port_name,
                   strerror(errno));
    return kInvalidPort;
  }
  auto close_on_exit = defer_at[&] { close(fd); };

  if (!SetOptions(fd, serial_init)) {
    return kInvalidPort;
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

void UnixSerialPort::Close() {
  if (fd_ != kInvalidPort) {
    close(fd_);
    fd_ = kInvalidPort;
  }
}

}  // namespace ae

#endif
