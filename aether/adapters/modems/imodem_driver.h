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

#include "aether/types/address.h"
#include "aether/adapters/modems/serial_ports/iserial_port.h"

namespace ae {

enum class kModemError : std::int8_t {
  kNoError = 0,
  kAtCommandError = -1,
  kBaudRateError = -2,
  kDbmaToHexBand = -3,
  kDbmaToHexRange = -4,
  kHexToDbmaBand = -5,
  kHexToDbmaRange = -6,
  kSetTxPowerBand = -7,
  kGetTxPowerBand = -8,
  kCheckSimStatus = -9,
  kPinWrong = -10,
  kSetupSim = -11,
  kSetNetMode = -12,
  kSetNetwork = -13
};

enum class kModemMode : std::uint8_t {
  kModeAuto = 0,
  kModeGSMOnly = 1,
  kModeLTEOnly = 2,
  kModeGSMLTE = 3,
  kModeCatM = 4,
  kModeNbIot = 5,
  kModeCatMNbIot = 6
};

enum class kAuthType : std::uint8_t {
  kAuthTypeNone = 0,
  kAuthTypePAP = 1,
  kAuthTypeCHAP = 2,
  kAuthTypePAPCHAP = 3
};

struct ModemInit {
  AE_REFLECT_MEMBERS(serial_init, pin, use_pin, operator_name, apn_name,
                     apn_user, apn_pass, modem_mode, auth_type, use_auth,
                     auth_user, auth_pass, ssl_cert, use_ssl)
  SerialInit serial_init;
  std::uint8_t pin[4];
  bool use_pin;
  kModemMode modem_mode;
  std::string operator_code;
  std::string operator_name;
  std::string apn_name;
  std::string apn_user;
  std::string apn_pass;
  kAuthType auth_type;
  bool use_auth;
  std::string auth_user;
  std::string auth_pass;
  std::string ssl_cert;
  bool use_ssl;
};

class IModemDriver {

 public:
  IModemDriver() = default;
  virtual ~IModemDriver() = default;

  virtual void Init() = 0;
  virtual void Setup() = 0;
  virtual void Stop() = 0;
  virtual void OpenNetwork(ae::Protocol protocol, std::string host, std::uint16_t port) = 0;
  virtual void WritePacket(std::vector<uint8_t> const& data) = 0;
  virtual void ReadPacket(std::vector<std::uint8_t>& data) = 0;

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