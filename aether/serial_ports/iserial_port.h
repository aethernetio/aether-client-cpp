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

#ifndef AETHER_SERIAL_PORTS_ISERIAL_PORT_H_
#define AETHER_SERIAL_PORTS_ISERIAL_PORT_H_

#include <optional>
#include <mutex>

#include "aether/serial_ports/serial_port_types.h"

#include "aether/types/data_buffer.h"
#include "aether/ptr/ptr_view.h"
#include "aether/poller/poller.h"
//#include "aether/actions/action.h"
//#include "aether/actions/action_ptr.h"
#include "aether/actions/action_context.h"


namespace ae {
/**
 * \brief Represents serial port interface for device communication.
 */
class ISerialPort {
 protected:
  ISerialPort() = default;

 public:
  explicit ISerialPort(ActionContext action_context, IPoller::ptr const& poller,
                       SerialInit const& serial_init);
  virtual ~ISerialPort() = default;

  /**
   * \brief Write amount of data.
   */
  virtual void Write(DataBuffer const& data) = 0;
  /**
   * \brief Read all the data.
   */
  virtual std::optional<DataBuffer> Read() = 0;
  /**
   * \brief Check if the serial port is open.
   */
  virtual bool IsOpen() = 0;

 private:
  ActionContext action_context_;
  PtrView<IPoller> poller_;
  SerialInit serial_init_;
};
} /* namespace ae */

#endif  // AETHER_SERIAL_PORTS_ISERIAL_PORT_H_
