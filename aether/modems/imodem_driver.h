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

#ifndef AETHER_MODEMS_IMODEM_DRIVER_H_
#define AETHER_MODEMS_IMODEM_DRIVER_H_

#include "aether/config.h"

#if AE_SUPPORT_MODEMS
#  include <string>
#  include <cstdint>

#  include "aether/types/address.h"
#  include "aether/events/events.h"
#  include "aether/actions/action.h"
#  include "aether/types/data_buffer.h"
#  include "aether/actions/action_ptr.h"
#  include "aether/actions/notify_action.h"
#  include "aether/actions/promise_action.h"
#  include "aether/modems/modem_driver_types.h"

namespace ae {
class IModemDriver {
 public:
  using ModemOperation = NotifyAction;
  using WriteOperation = NotifyAction;
  using OpenNetworkOperation = PromiseAction<ConnectionIndex>;
  using DataEvent = Event<void(ConnectionIndex, DataBuffer const& data)>;

  virtual ~IModemDriver() = default;

  virtual ActionPtr<ModemOperation> Start() = 0;
  virtual ActionPtr<ModemOperation> Stop() = 0;
  virtual ActionPtr<OpenNetworkOperation> OpenNetwork(Protocol protocol,
                                                      std::string const& host,
                                                      std::uint16_t port) = 0;
  virtual ActionPtr<ModemOperation> CloseNetwork(
      ConnectionIndex connect_index) = 0;
  virtual ActionPtr<WriteOperation> WritePacket(ConnectionIndex connect_index,
                                                DataBuffer const& data) = 0;
  virtual DataEvent::Subscriber data_event() = 0;

  virtual ActionPtr<ModemOperation> SetPowerSaveParam(
      ModemPowerSaveParam const& psp) = 0;
  virtual ActionPtr<ModemOperation> PowerOff() = 0;
};

} /* namespace ae */
#endif
#endif  // AETHER_MODEMS_IMODEM_DRIVER_H_
