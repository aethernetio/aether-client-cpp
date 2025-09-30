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

#include "aether/serial_ports/iserial_port.h"
#include "aether/serial_ports/serial_ports_tele.h"

namespace ae {

ISerialPort::ISerialPort(ActionContext action_context,
                         IPoller::ptr const& poller,
                         SerialInit const& serial_init)
    : action_context_{std::move(action_context)},
      poller_{poller},
      serial_init_{std::move(serial_init)},
      send_queue_manager_{action_context_},
      notify_error_action_{action_context_} {
  AE_TELE_INFO(kSerialTransport);
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable = false;

  Connect();
}

ISerialPort::~ISerialPort() { Disconnect(); }

} /* namespace ae */
