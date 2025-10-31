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

#ifndef AETHER_SERIAL_PORTS_SERIAL_PORT_FACTORY_H_
#define AETHER_SERIAL_PORTS_SERIAL_PORT_FACTORY_H_

#include <memory>

#include "aether/poller/poller.h"
#include "aether/actions/action_context.h"
#include "aether/serial_ports/iserial_port.h"
#include "aether/serial_ports/serial_port_types.h"

namespace ae {
class SerialPortFactory {
 public:
  static std::unique_ptr<ISerialPort> CreatePort(ActionContext action_context,
                                                 IPoller::ptr const& poller,
                                                 SerialInit const& serial_init);
};
}  // namespace ae

#endif  // AETHER_SERIAL_PORTS_SERIAL_PORT_FACTORY_H_
