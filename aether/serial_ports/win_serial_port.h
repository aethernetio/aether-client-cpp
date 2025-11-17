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

#ifndef AETHER_SERIAL_PORTS_WIN_SERIAL_PORT_H_
#define AETHER_SERIAL_PORTS_WIN_SERIAL_PORT_H_

#if defined _WIN32

#  include <mutex>
#  include <atomic>

#  include "aether/ptr/ptr_view.h"
#  include "aether/actions/action.h"
#  include "aether/actions/action_ptr.h"
#  include "aether/actions/action_context.h"

#  include "aether/poller/poller.h"
#  include "aether/poller/win_poller.h"
#  include "aether/serial_ports/iserial_port.h"
#  include "aether/serial_ports/serial_port_types.h"

#  include <Windows.h>

#  define WIN_SERIAL_PORT_ENABLED 1

namespace ae {
class WinSerialPort final : public ISerialPort {
  class ReadAction final : public Action<ReadAction> {
   public:
    ReadAction(ActionContext action_context, WinSerialPort& serial_port);

    UpdateStatus Update();

   private:
    void PollEvent(PollerEvent event);
    void ReadData();

    void RequestRead();
    void HandleRead();

    WinSerialPort* serial_port_;
    Subscription poll_sub_;
    std::list<DataBuffer> buffers_;
    std::atomic_bool read_event_;

    DataBuffer read_buffer_;
    WinPollerOverlapped overlapped_rd_{};

    static constexpr int kReadBufSize{4096};
    static constexpr int kReadTimeOutMSec{250};
  };

 public:
  explicit WinSerialPort(ActionContext action_context, SerialInit serial_init,
                         IPoller::ptr const& poller);
  ~WinSerialPort() override;

  void Write(DataBuffer const& data) override;

  DataReadEvent::Subscriber read_event() override;

  bool IsOpen() override;

 private:
  static void* OpenPort(SerialInit const& serial_init);
  static bool SetOptions(void* fd, SerialInit const& serial_init);

  void Close();

  ActionContext action_context_;
  SerialInit serial_init_;
  PtrView<IPoller> poller_;

  std::mutex fd_lock_;
  void* fd_;
  DataReadEvent read_event_;

  OVERLAPPED overlapped_wr_{};
  DWORD signal_;

  ActionPtr<ReadAction> read_action_;

  static constexpr int kReadBufSize{4096};
  static constexpr int kReadTimeOutMSec{250};
};
} /* namespace ae */

#endif  // _WIN32
#endif  // AETHER_SERIAL_PORTS_WIN_SERIAL_PORT_H_
