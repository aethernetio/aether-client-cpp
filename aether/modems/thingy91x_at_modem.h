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
#include "aether/adapters/modem_adapter.h"
#include "aether/serial_ports/iserial_port.h"
#include "aether/serial_ports/at_comm_support.h"

namespace ae {

struct Thingy91xConnection {
  std::int32_t handle;
  ae::Protocol protocol;
  std::string host;
  std::uint16_t port;
};

class Thingy91xAtModem final : public IModemDriver {
 public:
  explicit Thingy91xAtModem(ActionContext action_context, IPoller::ptr poller,
                            ModemInit modem_init);
  ~Thingy91xAtModem() override;

  ActionPtr<ModemOperation> Init() override;
  ActionPtr<ModemOperation> Start() override;
  ActionPtr<ModemOperation> Stop() override;
  ActionPtr<OpenNetworkOperation> OpenNetwork(ae::Protocol protocol,
                                              std::string const& host,
                                              std::uint16_t port) override;
  ActionPtr<ModemOperation> CloseNetwork(
      ConnectionIndex connect_index) override;
  ActionPtr<ModemOperation> WritePacket(ConnectionIndex connect_index,
                                        DataBuffer const& data) override;

  DataEvent::Subscriber data_event() override;

  ActionPtr<ModemOperation> SetPowerSaveParam(
      PowerSaveParam const& psp) override;
  ActionPtr<ModemOperation> PowerOff() override;

 private:
  ActionContext action_context_;
  ModemInit modem_init_;
  std::unique_ptr<ISerialPort> serial_;
  AtCommSupport at_comm_support_;
  std::vector<Thingy91xConnection> connect_vec_;

  static constexpr std::uint16_t kModemMTU{1024};

  ActionPtr<ModemOperation> CheckSimStatus();
  ActionPtr<ModemOperation> SetupSim(std::uint16_t pin);
  ActionPtr<ModemOperation> SetNetMode(kModemMode const modem_mode);
  ActionPtr<ModemOperation> SetupNetwork(
      std::string const& operator_name, std::string const& operator_code,
      std::string const& apn_name, std::string const& apn_user,
      std::string const& apn_pass, kModemMode modem_mode, kAuthType auth_type);
  ActionPtr<ModemOperation> SetTxPower(kModemMode modem_mode,
                                       std::vector<BandPower> const& power);
  ActionPtr<ModemOperation> GetTxPower(kModemMode modem_mode,
                                       std::vector<BandPower>& power);
  ActionPtr<ModemOperation> SetPsm(std::uint8_t psm_mode,
                                   kRequestedPeriodicTAUT3412 psm_tau,
                                   kRequestedActiveTimeT3324 psm_active);
  ActionPtr<ModemOperation> SetEdrx(EdrxMode edrx_mode, EdrxActTType act_type,
                                    kEDrx edrx_val);
  ActionPtr<ModemOperation> SetRai(std::uint8_t rai_mode);
  ActionPtr<ModemOperation> SetBandLock(std::uint8_t bl_mode,
                                        std::vector<std::int32_t> const& bands);
  ActionPtr<ModemOperation> ResetModemFactory(std::uint8_t res_mode);

  std::int32_t OpenTcpConnection(std::string const& host, std::uint16_t port);
  std::int32_t OpenUdpConnection();
  void SendTcp(Thingy91xConnection const& connection, DataBuffer const& data);
  void SendUdp(Thingy91xConnection const& connection, DataBuffer const& data);
  DataBuffer ReadTcp(Thingy91xConnection const& connection, Duration timeout);
  DataBuffer ReadUdp(Thingy91xConnection const& connection, Duration timeout);
};

} /* namespace ae */

#endif  // AETHER_MODEMS_THINGY91X_AT_MODEM_H_
