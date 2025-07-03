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

#ifndef AETHER_ADAPTERS_MODEMS_SIM7070_AT_MODEM_H_
#define AETHER_ADAPTERS_MODEMS_SIM7070_AT_MODEM_H_

#include <chrono>
#include <memory>

#include "aether/adapters/parent_modem.h"
#include "aether/adapters/modems/serial_ports/iserial_port.h"

namespace ae {

class Sim7070AtModem : public IModemDriver {
 public:
  explicit Sim7070AtModem(ModemInit modem_init);
  void Init() override;
  void Setup() override;
  void Stop() override;
  void OpenNetwork(ae::Protocol protocol, std::string host,
                   std::uint16_t port) override;
  void WritePacket(std::vector<uint8_t> const& data) override;
  void ReadPacket(std::vector<std::uint8_t>& data) override;
  void PowerOff();

 private:
  ModemInit modem_init_;
  std::unique_ptr<ISerialPort> serial_;

  kModemError CheckResponce(std::string responce, std::uint32_t wait_time,
                            std::string error_message);
  kModemError SetBaudRate(std::uint32_t rate);
  kModemError CheckSimStatus();
  kModemError SetupSim(const std::uint8_t pin[4]);
  kModemError SetNetMode(kModemMode modem_mode);
  kModemError SetupNetwork(std::string operator_name, std::string operator_code,
                           std::string apn_name, std::string apn_user,
                           std::string apn_pass, kModemMode modem_mode,
                           kAuthType auth_type);
  void sendATCommand(const std::string& command);
  bool waitForResponse(const std::string& expected,
                       std::chrono::milliseconds timeout_ms);
};

} /* namespace ae */

#endif  // AETHER_ADAPTERS_MODEMS_SIM7070_AT_MODEM_H_