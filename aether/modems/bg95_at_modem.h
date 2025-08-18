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
#include "aether/serial_ports/iserial_port.h"

namespace ae {

static const std::map<kModemBaudRate, std::string> baud_rate_commands_bg95 = {
    {kModemBaudRate::kBaudRate9600, "AT+IPR=9600"},
    {kModemBaudRate::kBaudRate19200, "AT+IPR=19200"},
    {kModemBaudRate::kBaudRate38400, "AT+IPR=38400"},
    {kModemBaudRate::kBaudRate57600, "AT+IPR=57600"},
    {kModemBaudRate::kBaudRate115200, "AT+IPR=115200"},
    {kModemBaudRate::kBaudRate230400, "AT+IPR=230400"},
    {kModemBaudRate::kBaudRate921600, "AT+IPR=921600"},
    {kModemBaudRate::kBaudRate2900000, "AT+IPR=2900000"},
    {kModemBaudRate::kBaudRate3000000, "AT+IPR=3000000"},
    {kModemBaudRate::kBaudRate3200000, "AT+IPR=3200000"},
    {kModemBaudRate::kBaudRate3684000, "AT+IPR=3684000"},
    {kModemBaudRate::kBaudRate4000000, "AT+IPR=4000000"}};

static const std::map<kModemBand, std::string> set_band_power_bg95 = {
    {kModemBand::kWCDMA_B1, "AT+QNVW=539,0,\"9B9EA0{}A3A5A6\""},
    {kModemBand::kWCDMA_B2, "AT+QNVW=1183,0,\"9C9EA0{}A3A5A6\""},
    {kModemBand::kWCDMA_B4, "AT+QNVW=4035,0,\"9C9EA0{}A3A5A6\""},
    {kModemBand::kWCDMA_B5, "AT+QNVW=1863,0,\"9C9EA0{}A3A5A6\""},
    {kModemBand::kWCDMA_B8, "AT+QNVW=3690,0,\"9C9EA0{}A3A5A6\""},
    {kModemBand::kLTE_B1, "AT+QNVFW=\"/nv/item_files/rfnv/00020992\",0100{}00"},
    {kModemBand::kLTE_B2, "AT+QNVFW=\"/nv/item_files/rfnv/00020993\",0100{}00"},
    {kModemBand::kLTE_B3, "AT+QNVFW=\"/nv/item_files/rfnv/00020994\",0100{}00"},
    {kModemBand::kLTE_B4, "AT+QNVFW=\"/nv/item_files/rfnv/00020995\",0100{}00"},
    {kModemBand::kLTE_B5, "AT+QNVFW=\"/nv/item_files/rfnv/00020996\",0100{}00"},
    {kModemBand::kLTE_B7, "AT+QNVFW=\"/nv/item_files/rfnv/00020997\",0100{}00"},
    {kModemBand::kLTE_B8, "AT+QNVFW=\"/nv/item_files/rfnv/00020998\",0100{}00"},
    {kModemBand::kLTE_B12,
     "AT+QNVFW=\"/nv/item_files/rfnv/00022191\",0100{}00"},
    {kModemBand::kLTE_B13,
     "AT+QNVFW=\"/nv/item_files/rfnv/00021000\",0100{}00"},
    {kModemBand::kLTE_B17,
     "AT+QNVFW=\"/nv/item_files/rfnv/00021001\",0100{}00"},
    {kModemBand::kLTE_B18,
     "AT+QNVFW=\"/nv/item_files/rfnv/00021002\",0100{}00"},
    {kModemBand::kLTE_B19,
     "AT+QNVFW=\"/nv/item_files/rfnv/00023133\",0100{}00"},
    {kModemBand::kLTE_B20,
     "AT+QNVFW=\"/nv/item_files/rfnv/00021003\",0100{}00"},
    {kModemBand::kLTE_B25,
     "AT+QNVFW=\"/nv/item_files/rfnv/00022351\",0100{}00"},
    {kModemBand::kLTE_B26,
     "AT+QNVFW=\"/nv/item_files/rfnv/00024674\",0100{}00"},
    {kModemBand::kLTE_B28,
     "AT+QNVFW=\"/nv/item_files/rfnv/00025477\",0100{}00"},
    {kModemBand::kLTE_B38,
     "AT+QNVFW=\"/nv/item_files/rfnv/00021004\",0100{}00"},
    {kModemBand::kLTE_B39,
     "AT+QNVFW=\"/nv/item_files/rfnv/00023620\",0100{}00"},
    {kModemBand::kLTE_B40,
     "AT+QNVFW=\"/nv/item_files/rfnv/00021005\",0100{}00"},
    {kModemBand::kLTE_B41,
     "AT+QNVFW=\"/nv/item_files/rfnv/00021670\",0100{}00"},
    {kModemBand::kTDS_B34,
     "AT+QNVFW=\"/nv/item_files/rfnv/00022622\",0100{}00"},
    {kModemBand::kTDS_B39,
     "AT+QNVFW=\"/nv/item_files/rfnv/00022663\",0100{}00"},
    {kModemBand::kGSM_850,
     "AT+QNVFW=\"/nv/item_files/rfnv/"
     "00025076\",C2018A0252031A04E204AA0572063A070208CA0892095A0A220BEA0B{}{}"},
    {kModemBand::kGSM_900,
     "AT+QNVFW=\"/nv/item_files/rfnv/"
     "00025077\",C2018A0252031A04E204AA0572063A070208CA0892095A0A220BEA0B{}{}"},
    {kModemBand::kGSM_1800,
     "AT+QNVFW=\"/nv/item_files/rfnv/"
     "00025078\",000096005E012602EE02B6037E0446050E06D6069E0766082E09F609BE0A{"
     "}"},
    {kModemBand::kGSM_1900,
     "AT+QNVFW=\"/nv/item_files/rfnv/"
     "00025079\",000096005E012602EE02B6037E0446050E06D6069E0766082E09F609BE0A{"
     "}"},
    {kModemBand::kTDSCDMA_B34, "AT+QNVFW=\"/nv/item_files/rfnv/00022622\",{}"},
    {kModemBand::kTDSCDMA_B39, "AT+QNVFW=\"/nv/item_files/rfnv/00022663\",{}"}};

static const std::map<kModemBand, std::string> get_band_power_bg95 = {
    {kModemBand::kWCDMA_B1, "AT+QNVR=539,0"},
    {kModemBand::kWCDMA_B2, "AT+QNVR=1183,0"},
    {kModemBand::kWCDMA_B4, "AT+QNVR=4035,0"},
    {kModemBand::kWCDMA_B5, "AT+QNVR=1863,0"},
    {kModemBand::kWCDMA_B8, "AT+QNVR=3690,0"},
    {kModemBand::kLTE_B1, "AT+QNVFR=\"/nv/item_files/rfnv/00020992\",0100"},
    {kModemBand::kLTE_B2, "AT+QNVFR=\"/nv/item_files/rfnv/00020993\",0100"},
    {kModemBand::kLTE_B3, "AT+QNVFR=\"/nv/item_files/rfnv/00020994\",0100"},
    {kModemBand::kLTE_B4, "AT+QNVFR=\"/nv/item_files/rfnv/00020995\",0100"},
    {kModemBand::kLTE_B5, "AT+QNVFR=\"/nv/item_files/rfnv/00020996\",0100"},
    {kModemBand::kLTE_B7, "AT+QNVFR=\"/nv/item_files/rfnv/00020997\",0100"},
    {kModemBand::kLTE_B8, "AT+QNVFR=\"/nv/item_files/rfnv/00020998\",0100"},
    {kModemBand::kLTE_B12, "AT+QNVFR=\"/nv/item_files/rfnv/00022191\",0100"},
    {kModemBand::kLTE_B13, "AT+QNVFR=\"/nv/item_files/rfnv/00021000\",0100"},
    {kModemBand::kLTE_B17, "AT+QNVFR=\"/nv/item_files/rfnv/00021001\",0100"},
    {kModemBand::kLTE_B18, "AT+QNVFR=\"/nv/item_files/rfnv/00021002\",0100"},
    {kModemBand::kLTE_B19, "AT+QNVFR=\"/nv/item_files/rfnv/00023133\",0100"},
    {kModemBand::kLTE_B20, "AT+QNVFR=\"/nv/item_files/rfnv/00021003\",0100"},
    {kModemBand::kLTE_B25, "AT+QNVFR=\"/nv/item_files/rfnv/00022351\",0100"},
    {kModemBand::kLTE_B26, "AT+QNVFR=\"/nv/item_files/rfnv/00024674\",0100"},
    {kModemBand::kLTE_B28, "AT+QNVFR=\"/nv/item_files/rfnv/00025477\",0100"},
    {kModemBand::kLTE_B38, "AT+QNVFR=\"/nv/item_files/rfnv/00021004\",0100"},
    {kModemBand::kLTE_B39, "AT+QNVFR=\"/nv/item_files/rfnv/00023620\",0100"},
    {kModemBand::kLTE_B40, "AT+QNVFR=\"/nv/item_files/rfnv/00021005\",0100"},
    {kModemBand::kLTE_B41, "AT+QNVFR=\"/nv/item_files/rfnv/00021670\",0100"},
    {kModemBand::kTDS_B34, "AT+QNVFR=\"/nv/item_files/rfnv/00022622\",0100"},
    {kModemBand::kTDS_B39, "AT+QNVFR=\"/nv/item_files/rfnv/00022663\",0100"},
    {kModemBand::kGSM_850, "AT+QNVFR=\"/nv/item_files/rfnv/00025076\""},
    {kModemBand::kGSM_900, "AT+QNVFR=\"/nv/item_files/rfnv/00025077\""},
    {kModemBand::kGSM_1800, "AT+QNVFR=\"/nv/item_files/rfnv/00025078\""},
    {kModemBand::kGSM_1900, "AT+QNVFR=\"/nv/item_files/rfnv/00025079\""},
    {kModemBand::kTDSCDMA_B34, "AT+QNVFR=\"/nv/item_files/rfnv/00022622\""},
    {kModemBand::kTDSCDMA_B39, "AT+QNVFR=\"/nv/item_files/rfnv/00022663\""}};

class Bg95AtModem final : public IModemDriver {
  AE_OBJECT(Bg95AtModem, IModemDriver, 0)

 protected:
  Bg95AtModem() = default;

 public:
  explicit Bg95AtModem(ModemInit modem_init);
  void Init() override;
  void Start() override;
  void Stop() override;
  void OpenNetwork(std::int8_t& connect_index, ae::Protocol const protocol,
                   std::string const host, std::uint16_t const port) override;
  void CloseNetwork(std::int8_t const connect_index) override;
  void WritePacket(std::int8_t const connect_index,
                   std::vector<uint8_t> const& data) override;
  void ReadPacket(std::int8_t const connect_index,
                  std::vector<std::uint8_t>& data,
                  std::int32_t const timeout) override;
  void PollSockets(std::int8_t const connect_index, PollResult& results,
                   std::int32_t const timeout) override;
  void PowerOff();

 private:
  ModemInit modem_init_;
  std::unique_ptr<ISerialPort> serial_;

  kModemError CheckResponce(std::string responce, std::uint32_t wait_time,
                            std::string error_message);
  kModemError SetBaudRate(kModemBaudRate rate);
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