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

#ifndef AETHER_MODEMS_SIM7070_AT_MODEM_H_
#define AETHER_MODEMS_SIM7070_AT_MODEM_H_

#include <memory>

#include "aether/modems/imodem_driver.h"
#include "aether/serial_ports/iserial_port.h"

namespace ae {

struct ConnectionHandle {
  AE_REFLECT_MEMBERS(context_index, connect_index)
  std::int32_t context_index;
  std::int32_t connect_index;
};

struct Sim7070Connection {
  AE_REFLECT_MEMBERS(handle, protocol, host, port)
  ae::ConnectionHandle handle;
  ae::Protocol protocol;
  std::string host;
  std::uint16_t port;
};

static const std::map<kBaudRate, std::string> baud_rate_commands_sim7070 = {
    {kBaudRate::kBaudRate0, "AT+IPR=0"},
    {kBaudRate::kBaudRate300, "AT+IPR=300"},
    {kBaudRate::kBaudRate600, "AT+IPR=600"},
    {kBaudRate::kBaudRate1200, "AT+IPR=1200"},
    {kBaudRate::kBaudRate2400, "AT+IPR=2400"},
    {kBaudRate::kBaudRate4800, "AT+IPR=4800"},
    {kBaudRate::kBaudRate9600, "AT+IPR=9600"},
    {kBaudRate::kBaudRate19200, "AT+IPR=19200"},
    {kBaudRate::kBaudRate38400, "AT+IPR=38400"},
    {kBaudRate::kBaudRate57600, "AT+IPR=57600"},
    {kBaudRate::kBaudRate115200, "AT+IPR=115200"},
    {kBaudRate::kBaudRate230400, "AT+IPR=230400"},
    {kBaudRate::kBaudRate921600, "AT+IPR=921600"},
    {kBaudRate::kBaudRate2000000, "AT+IPR=2000000"},
    {kBaudRate::kBaudRate2900000, "AT+IPR=2900000"},
    {kBaudRate::kBaudRate3000000, "AT+IPR=3000000"},
    {kBaudRate::kBaudRate3200000, "AT+IPR=3200000"},
    {kBaudRate::kBaudRate3684000, "AT+IPR=3684000"},
    {kBaudRate::kBaudRate4000000, "AT+IPR=4000000"}};

class Sim7070AtModem final : public IModemDriver {
 protected:
  Sim7070AtModem() = default;

 public:
  explicit Sim7070AtModem(ModemInit modem_init);
  ~Sim7070AtModem() override;

  bool Init() override;
  bool Start() override;
  bool Stop() override;
  ConnectionIndex OpenNetwork(ae::Protocol protocol, std::string const& host,
                              std::uint16_t port) override;
  void CloseNetwork(ae::ConnectionIndex connect_index) override;
  void WritePacket(ae::ConnectionIndex connect_index,
                   ae::DataBuffer const& data) override;
  DataBuffer ReadPacket(ae::ConnectionIndex connect_index,
                        ae::Duration timeout) override;
  bool SetPowerSaveParam(ae::PowerSaveParam const& psp) override;
  bool PowerOff() override;

 private:
  ModemInit modem_init_;
  std::unique_ptr<ISerialPort> serial_;
  std::vector<Sim7070Connection> connect_vec_;

  static constexpr std::uint16_t kModemMTU{1520};

  kModemError CheckResponse(std::string const& response,
                            std::uint32_t const wait_time,
                            std::string const& error_message);
  kModemError SetBaudRate(kBaudRate rate);
  kModemError CheckSimStatus();
  kModemError SetupSim(const std::uint8_t pin[4]);
  kModemError SetNetMode(kModemMode modem_mode);
  kModemError SetupNetwork(std::string const& operator_name,
                           std::string const& operator_code,
                           std::string const& apn_name,
                           std::string const& apn_user,
                           std::string const& apn_pass, kModemMode modem_mode,
                           kAuthType auth_type);
  kModemError SetupProtoPar();

  ConnectionHandle OpenTcpConnection(std::string const& host,
                                     std::uint16_t port);
  ConnectionHandle OpenUdpConnection(std::string const& host,
                                     std::uint16_t port);
  void SendTcp(Sim7070Connection const& connection, DataBuffer const& data);
  void SendUdp(Sim7070Connection const& connection, DataBuffer const& data);
  DataBuffer ReadTcp(Sim7070Connection const& connection);
  DataBuffer ReadUdp(Sim7070Connection const& connection);

  void SendATCommand(const std::string& command);
  bool WaitForResponse(const std::string& expected, Duration timeout);
  std::string PinToString(const std::uint8_t pin[4]);
};

} /* namespace ae */

#endif  // AETHER_MODEMS_SIM7070_AT_MODEM_H_
