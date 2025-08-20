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
#include "aether/serial_ports/iserial_port.h"

namespace ae {

struct Sim7070Connection {
  std::uint32_t context_index;
  std::uint32_t connect_index;
};

static const std::map<kModemBaudRate, std::string> baud_rate_commands_sim7070 =
    {{kModemBaudRate::kBaudRate0, "AT+IPR=0"},
     {kModemBaudRate::kBaudRate300, "AT+IPR=300"},
     {kModemBaudRate::kBaudRate600, "AT+IPR=600"},
     {kModemBaudRate::kBaudRate1200, "AT+IPR=1200"},
     {kModemBaudRate::kBaudRate2400, "AT+IPR=2400"},
     {kModemBaudRate::kBaudRate4800, "AT+IPR=4800"},
     {kModemBaudRate::kBaudRate9600, "AT+IPR=9600"},
     {kModemBaudRate::kBaudRate19200, "AT+IPR=19200"},
     {kModemBaudRate::kBaudRate38400, "AT+IPR=38400"},
     {kModemBaudRate::kBaudRate57600, "AT+IPR=57600"},
     {kModemBaudRate::kBaudRate115200, "AT+IPR=115200"},
     {kModemBaudRate::kBaudRate230400, "AT+IPR=230400"},
     {kModemBaudRate::kBaudRate921600, "AT+IPR=921600"},
     {kModemBaudRate::kBaudRate2000000, "AT+IPR=2000000"},
     {kModemBaudRate::kBaudRate2900000, "AT+IPR=2900000"},
     {kModemBaudRate::kBaudRate3000000, "AT+IPR=3000000"},
     {kModemBaudRate::kBaudRate3200000, "AT+IPR=3200000"},
     {kModemBaudRate::kBaudRate3684000, "AT+IPR=3684000"},
     {kModemBaudRate::kBaudRate4000000, "AT+IPR=4000000"}};

class Sim7070AtModem final : public IModemDriver {
  
  AE_OBJECT(Sim7070AtModem, IModemDriver, 0)

 protected:
  Sim7070AtModem() = default;
  
 public:
  explicit Sim7070AtModem(ModemInit modem_init, Domain* domain);
  AE_OBJECT_REFLECT(AE_MMBRS(modem_init_))
  
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
  /*void PollSockets(std::int8_t const connect_index, PollResult& results,
                   std::int32_t const timeout) override;*/
  void PowerOff();

 private:
  std::unique_ptr<ISerialPort> serial_;
  std::vector<Sim7070Connection> connect_vec_;

  kModemError CheckResponce(std::string responce, std::uint32_t wait_time,
                            std::string error_message);
  kModemError SetBaudRate(kModemBaudRate rate);
  kModemError CheckSimStatus();
  kModemError SetupSim(const std::uint8_t pin[4]);
  kModemError SetNetMode(kModemMode modem_mode);
  kModemError SetupNetwork(std::string operator_name, std::string operator_code,
                           std::string apn_name, std::string apn_user,
                           std::string apn_pass, kModemMode modem_mode,
                           kAuthType auth_type);
  kModemError SetupProtoPar();
  void SendATCommand(const std::string& command);
  bool WaitForResponse(const std::string& expected,
                       std::chrono::milliseconds timeout_ms);
  std::string PinToString(const std::uint8_t pin[4]);
};

} /* namespace ae */

#endif  // AETHER_ADAPTERS_MODEMS_SIM7070_AT_MODEM_H_