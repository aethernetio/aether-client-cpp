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

#ifndef AETHER_ADAPTERS_MODEMS_IMODEM_DRIVER_H_
#define AETHER_ADAPTERS_MODEMS_IMODEM_DRIVER_H_

#include <functional>
#include <string>
#include <map>

#include "aether/types/address.h"
#include "aether/types/modem_driver_types.h"
#include "aether/serial_ports/iserial_port.h"

namespace ae {

class IModemDriver : public Obj{
  AE_OBJECT(IModemDriver, Obj, 0)

 protected:
  IModemDriver() = default;
  
 public:
#ifdef AE_DISTILLATION
  explicit IModemDriver(Domain* domain);
#endif  // AE_DISTILLATION
  AE_OBJECT_REFLECT()

  virtual ~IModemDriver() = default;

  virtual void Init() = 0;
  virtual void Start() = 0;
  virtual void Stop() = 0;
  virtual void OpenNetwork(std::int8_t& connect_index,
                           ae::Protocol const protocol, const std::string host,
                           const std::uint16_t port) = 0;
  virtual void CloseNetwork(std::int8_t const connect_index) = 0;
  virtual void WritePacket(std::int8_t const connect_index,
                           std::vector<uint8_t> const& data) = 0;
  virtual void ReadPacket(std::int8_t const connect_index,
                          std::vector<std::uint8_t>& data,
                          std::int32_t const timeout) = 0;
  virtual void PollSockets(std::int8_t const connect_index, PollResult& results,
                           std::int32_t const timeout) = 0;
  virtual void SetPowerSaveParam(kPowerSaveParam const& psp) = 0;
  virtual void PowerOff() = 0;

  Event<void(bool result)> modem_connected_event_;
  Event<void(int result)> modem_error_event_;

  std::string pinToString(const std::uint8_t pin[4]) {
    std::string result{};

    for (int i = 0; i < 4; ++i) {
      if (pin[i] > 9) {
        result = "ERROR";
        break;
      }
      result += static_cast<char>('0' + pin[i]);
    }

    return result;
  }
};

} /* namespace ae */

#endif  // AETHER_ADAPTERS_MODEMS_IMODEM_DRIVER_H_