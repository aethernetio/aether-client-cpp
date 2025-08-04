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

#ifndef AETHER_ADAPTERS_MODEMS_BG95_AT_MODEM_H_
#define AETHER_ADAPTERS_MODEMS_BG95_AT_MODEM_H_

#include <chrono>
#include <memory>

#include "aether/adapters/parent_modem.h"
#include "aether/adapters/modems/serial_ports/iserial_port.h"

namespace ae {

class Bg95AtModem : public IModemDriver {
 public:
  explicit Bg95AtModem(ModemInit modem_init);
  void Init() override;
  void Start() override;
  void Stop() override;
  void OpenNetwork(std::uint8_t const context_index,
                   std::uint8_t const connect_index,
                   ae::Protocol const protocol, std::string const host,
                   std::uint16_t const port) override;
  void CloseNetwork(std::uint8_t const context_index,
                    std::uint8_t const connect_index) override;
  void WritePacket(std::uint8_t const connect_index,
                   std::vector<uint8_t> const& data) override;
  void ReadPacket(std::uint8_t const connect_index,
                  std::vector<std::uint8_t>& data,
                  std::int32_t const timeout) override;
  void PollSocket(std::vector<std::uint32_t> const& handles,
                  std::int32_t const timeout) override;
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
                           std::string apn_pass);
  kModemError SetTxPower(kModemBand band, const float& power);
  kModemError GetTxPower(kModemBand band, float& power);
  kModemError DbmaToHex(kModemBand band, const float& power, std::string& hex);
  kModemError HexToDbma(kModemBand band, float& power, const std::string& hex);

  void sendATCommand(const std::string& command);
  bool waitForResponse(const std::string& expected,
                       std::chrono::milliseconds timeout_ms);
};

} /* namespace ae */

#endif  // AETHER_ADAPTERS_MODEMS_BG95_AT_MODEM_H_