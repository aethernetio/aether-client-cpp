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

#include <string>
#include <cstdint>

#include "aether/types/address.h"
#include "aether/types/data_buffer.h"
#include "aether/modems/modem_driver_types.h"

namespace ae {

class IModemDriver {
 public:
  virtual ~IModemDriver() = default;

  virtual bool Init() = 0;
  virtual bool Start() = 0;
  virtual bool Stop() = 0;
  virtual ConnectionIndex OpenNetwork(Protocol protocol,
                                      std::string const& host,
                                      std::uint16_t port) = 0;
  virtual void CloseNetwork(ConnectionIndex connect_index) = 0;
  virtual void WritePacket(ConnectionIndex connect_index,
                           DataBuffer const& data) = 0;
  virtual DataBuffer ReadPacket(ConnectionIndex connect_index,
                                Duration timeout) = 0;

  virtual bool SetPowerSaveParam(PowerSaveParam const& psp) = 0;
  virtual bool PowerOff() = 0;
};

} /* namespace ae */

#endif  // AETHER_MODEMS_IMODEM_DRIVER_H_
