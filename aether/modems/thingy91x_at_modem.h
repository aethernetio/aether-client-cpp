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

#ifndef AETHER_MODEMS_THINGY91X_AT_MODEM_H_
#define AETHER_MODEMS_THINGY91X_AT_MODEM_H_

#include <chrono>
#include <memory>

#include "aether/modems/imodem_driver.h"
#include "aether/serial_ports/iserial_port.h"

namespace ae {

struct Thingy91xConnection {
  std::int32_t handle;
  ae::Protocol protocol;
  std::string host;
  std::uint16_t port;
};

class Thingy91xAtModem final : public IModemDriver {
 protected:
  Thingy91xAtModem() = default;

 public:
  explicit Thingy91xAtModem(ModemInit modem_init);
  ~Thingy91xAtModem() override;

  bool Init() override;
  bool Start() override;
  bool Stop() override;
  ConnectionIndex OpenNetwork(ae::Protocol protocol, std::string const& host,
                              std::uint16_t port) override;
  void CloseNetwork(ConnectionIndex connect_index) override;
  void WritePacket(ConnectionIndex connect_index,
                   DataBuffer const& data) override;
  DataBuffer ReadPacket(ConnectionIndex connect_index,
                        Duration timeout) override;
  bool SetPowerSaveParam(ae::PowerSaveParam const& psp) override;
  bool PowerOff() override;

 private:
  ModemInit modem_init_;
  std::unique_ptr<ISerialPort> serial_;
  std::vector<Thingy91xConnection> connect_vec_;

  static constexpr std::uint16_t kModemMTU{1024};

  kModemError CheckResponse(std::string const response,
                            std::uint32_t const wait_time,
                            std::string const error_message);
  kModemError SetBaudRate(std::uint32_t const rate);
  kModemError CheckSimStatus();
  kModemError SetupSim(std::uint8_t const pin[4]);
  kModemError SetNetMode(kModemMode const modem_mode);
  kModemError SetupNetwork(std::string const operator_name,
                           std::string const operator_code,
                           std::string const apn_name,
                           std::string const apn_user,
                           std::string const apn_pass,
                           kModemMode const modem_mode,
                           kAuthType const auth_type);
  kModemError SetTxPower(kModemMode const modem_mode,
                         std::vector<BandPower> const& power);
  kModemError GetTxPower(kModemMode const modem_mode,
                         std::vector<BandPower>& power);
  kModemError SetPsm(std::uint8_t const psm_mode,
                     kRequestedPeriodicTAUT3412 const psm_tau,
                     kRequestedActiveTimeT3324 const psm_active);
  kModemError SetEdrx(EdrxMode const edrx_mode, EdrxActTType const act_type,
                      kEDrx const edrx_val);
  kModemError SetRai(std::uint8_t const rai_mode);
  kModemError SetBandLock(std::uint8_t const bl_mode,
                          std::vector<std::int32_t> const& bands);
  kModemError ResetModemFactory(std::uint8_t const res_mode);

  std::int32_t OpenTcpConnection(std::string const& host, std::uint16_t port);
  std::int32_t OpenUdpConnection();
  void SendTcp(Thingy91xConnection const& connection, DataBuffer const& data);
  void SendUdp(Thingy91xConnection const& connection, DataBuffer const& data);
  DataBuffer ReadTcp(Thingy91xConnection const& connection, Duration timeout);
  DataBuffer ReadUdp(Thingy91xConnection const& connection, Duration timeout);

  void SendATCommand(std::string const& command);
  bool WaitForResponse(std::string const& expected, Duration timeout);
  static int GetProtocolCode(Protocol protocol);

  std::string PinToString(const std::uint8_t pin[4]);
};

} /* namespace ae */

#endif  // AETHER_MODEMS_THINGY91X_AT_MODEM_H_
