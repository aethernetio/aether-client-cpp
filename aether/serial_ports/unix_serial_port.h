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

#ifndef AETHER_SERIAL_PORTS_UNIX_SERIAL_PORT_H_
#define AETHER_SERIAL_PORTS_UNIX_SERIAL_PORT_H_

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
    defined(__FreeBSD__)

#  define UNIX_SERIAL_PORT_ENABLED 1

#  include <list>
#  include <mutex>
#  include <atomic>

#  include "aether/ae_context.h"
#  include "aether/poller/poller.h"
#  include "aether/types/data_buffer.h"
#  include "aether/poller/unix_poller.h"
#  include "aether/serial_ports/iserial_port.h"
#  include "aether/serial_ports/serial_port_types.h"

namespace ae {
class UnixSerialPort final : public ISerialPort {
 public:
  explicit UnixSerialPort(AeContext const& ae_context, SerialInit serial_init,
                          IPoller::ptr const& poller);
  ~UnixSerialPort() override;

  void Write(std::span<std::uint8_t const> data) override;

  DataReadEvent::Subscriber read_event() override;

  bool IsOpen() override;

 private:
  static int OpenPort(SerialInit const& serial_init);
  static bool SetOptions(int fd, SerialInit const& serial_init);

  void PolleEvent(EventType event);
  void ReadData();
  void EmitData();

  void Close();

  AeContext ae_context_;
  SerialInit serial_init_;
  UnixPollerImpl* poller_;

  std::mutex fd_lock_;
  int fd_;
  DataReadEvent read_event_;

  std::list<DataBuffer> buffers_;
  std::atomic_bool read_flag_;
  TaskSubscription scheduler_sub_;
};
}  // namespace ae

#endif

#endif  // AETHER_SERIAL_PORTS_UNIX_SERIAL_PORT_H_
