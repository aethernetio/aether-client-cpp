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

#ifndef AETHER_ADAPTERS_MODEMS_THINGY91X_AT_MODEM_H_
#define AETHER_ADAPTERS_MODEMS_THINGY91X_AT_MODEM_H_

#include <chrono>
#include <memory>

#include "aether/adapters/parent_modem.h"
#include "aether/adapters/modems/serial_ports/iserial_port.h"

namespace ae {


class Thingy91xAtModem : public IModemDriver {
 public:
  explicit Thingy91xAtModem(ModemInit modem_init);
  void Init() override;
  void Start() override;
  void Stop() override;
  void OpenNetwork(std::uint32_t const context_index,
                   std::uint32_t const connect_index,
                   ae::Protocol const protocol, std::string const host,
                   std::uint16_t const port) override;
  void CloseNetwork(std::uint32_t const context_index,
                    std::uint32_t const connect_index) override;
  void WritePacket(std::uint32_t const connect_index,
                   std::vector<uint8_t> const& data) override;
  void ReadPacket(std::uint32_t const connect_index,
                  std::vector<std::uint8_t>& data,
                  std::int32_t const timeout) override;
  void GetHandles(std::vector<std::int32_t>& handles) override;
  void PollSockets(std::vector<std::int32_t> const& handles,
                   std::vector<PollResults>& results,
                   std::int32_t const timeout) override;
  void SetPowerSaveParam(kPowerSaveParam const& psp) override;
  void PowerOff() override;

 private:
  ModemInit modem_init_;
  std::unique_ptr<ISerialPort> serial_;
  ae::Protocol protocol_;
  std::string host_;
  std::uint16_t port_;
  std::vector<std::int32_t> handles_;

  kModemError CheckResponce(std::string const responce,
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
  void sendATCommand(std::string const& command);
  bool waitForResponse(std::string const& expected,
                       std::chrono::milliseconds const timeout_ms);
};

} /* namespace ae */

#endif  // AETHER_ADAPTERS_MODEMS_THINGY91X_AT_MODEM_H_