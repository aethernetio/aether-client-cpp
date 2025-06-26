
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
#include "aether/adapters/modems/imodem_driver.h"
#include "aether/adapters/modems/serial_ports/iserial_port.h"

namespace ae {

enum class kModemBand : std::uint8_t {
  kWCDMA_B1 = 0,
  kWCDMA_B2 = 1,
  kWCDMA_B4 = 2,
  kWCDMA_B5 = 3,
  kWCDMA_B8 = 4,
  kLTE_B1 = 5,
  kLTE_B2 = 6,
  kLTE_B3 = 7,
  kLTE_B4 = 8,
  kLTE_B5 = 9,
  kLTE_B7 = 10,
  kLTE_B8 = 11,
  kLTE_B12 = 12,
  kLTE_B13 = 13,
  kLTE_B17 = 14,
  kLTE_B18 = 15,
  kLTE_B19 = 16,
  kLTE_B20 = 17,
  kLTE_B25 = 18,
  kLTE_B26 = 19,
  kLTE_B28 = 20,
  kLTE_B38 = 21,
  kLTE_B39 = 22,
  kLTE_B40 = 23,
  kLTE_B41 = 24,
  kTDS_B34 = 25,
  kTDS_B39 = 26,
  kGSM_850 = 27,
  kGSM_900 = 28,
  kGSM_1800 = 29,
  kGSM_1900 = 30,
  kTDSCDMA_B34 = 31,
  kTDSCDMA_B39 = 32,
  kINVALID_BAND = 33
};

class Bg95AtModem : public IModemDriver {
 public:
  explicit Bg95AtModem(ModemInit modem_init);
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

  int CheckResponce(std::string responce, std::uint32_t wait_time,
                    std::string error_message);
  int SetBaudRate(std::uint32_t rate);
  int CheckSIMStatus();
  int SetupSim(const std::uint8_t pin[4]);
  int SetNetMode(kModemMode modem_mode);
  int SetupNetwork(std::string operator_name, std::string apn_name,
                   std::string apn_user, std::string apn_pass);
  int SetTxPower(kModemBand band, const float& power);
  int GetTxPower(kModemBand band, float& power);
  int DbmaToHex(kModemBand band, const float& power, std::string& hex);
  int HexToDbma(kModemBand band, float& power, const std::string& hex);
  void sendATCommand(const std::string& command);
  bool waitForResponse(const std::string& expected,
                       std::chrono::milliseconds timeout_ms);
};

} /* namespace ae */

#endif  // AETHER_ADAPTERS_MODEMS_BG95_AT_MODEM_H_