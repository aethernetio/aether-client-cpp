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

#include "aether/modems/imodem_driver.h"
#include "aether/serial_ports/iserial_port.h"

namespace ae {

struct Thingy91xConnection {
  AE_REFLECT_MEMBERS(handle)
  std::int32_t handle;
};

class Thingy91xAtModem final : public IModemDriver {
  AE_OBJECT(Thingy91xAtModem, IModemDriver, 0)

 protected:
  Thingy91xAtModem() = default;

 public:
  explicit Thingy91xAtModem(ModemInit modem_init, Domain* domain);
  AE_OBJECT_REFLECT(AE_MMBRS(connect_vec_, protocol_, host_, port_))

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
  void SetPowerSaveParam(ae::PowerSaveParam const& psp) override;
  void PowerOff() override;

 private:
  std::unique_ptr<ISerialPort> serial_;
  std::vector<Thingy91xConnection> connect_vec_;
  ae::Protocol protocol_;
  std::string host_;
  std::uint16_t port_;

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
  kModemError ParsePollEvents(const std::string& str,
                              std::vector<PollEvents>& events_out);
  void SendATCommand(std::string const& command);
  bool WaitForResponse(std::string const& expected,
                       std::chrono::milliseconds const timeout_ms);
  std::string PinToString(const std::uint8_t pin[4]);
};

} /* namespace ae */

#endif  // AETHER_ADAPTERS_MODEMS_THINGY91X_AT_MODEM_H_
