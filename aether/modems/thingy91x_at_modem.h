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

#include <set>
#include <memory>

#include "aether/poller/poller.h"
#include "aether/actions/pipeline.h"
#include "aether/actions/actions_queue.h"
#include "aether/actions/repeatable_task.h"
#include "aether/serial_ports/iserial_port.h"
#include "aether/serial_ports/at_support/at_support.h"

#include "aether/modems/imodem_driver.h"

namespace ae {
class Thingy91TcpOpenNetwork;
class Thingy91UdpOpenNetwork;

class Thingy91xAtModem final : public IModemDriver {
  friend class Thingy91TcpOpenNetwork;
  friend class Thingy91UdpOpenNetwork;
  static constexpr std::uint16_t kModemMTU{1024};

 public:
  explicit Thingy91xAtModem(ActionContext action_context,
                            IPoller::ptr const& poller, ModemInit modem_init);

  ActionPtr<ModemOperation> Start() override;
  ActionPtr<ModemOperation> Stop() override;
  ActionPtr<OpenNetworkOperation> OpenNetwork(ae::Protocol protocol,
                                              std::string const& host,
                                              std::uint16_t port) override;
  ActionPtr<ModemOperation> CloseNetwork(
      ConnectionIndex connect_index) override;
  ActionPtr<WriteOperation> WritePacket(ConnectionIndex connect_index,
                                        DataBuffer const& data) override;

  DataEvent::Subscriber data_event() override;

  ActionPtr<ModemOperation> SetPowerSaveParam(
      PowerSaveParam const& psp) override;
  ActionPtr<ModemOperation> PowerOff() override;

 private:
  void Init();

  ActionPtr<IPipeline> CheckSimStatus();
  ActionPtr<IPipeline> SetupSim(std::uint16_t pin);
  ActionPtr<IPipeline> SetNetMode(kModemMode modem_mode);
  ActionPtr<IPipeline> SetupNetwork(std::string const& operator_name,
                                    std::string const& operator_code,
                                    std::string const& apn_name,
                                    std::string const& apn_user,
                                    std::string const& apn_pass,
                                    kModemMode modem_mode, kAuthType auth_type);
  ActionPtr<IPipeline> SetTxPower(kModemMode modem_mode,
                                  std::vector<BandPower> const& power);
  ActionPtr<IPipeline> GetTxPower(kModemMode modem_mode,
                                  std::vector<BandPower>& power);
  ActionPtr<IPipeline> SetPsm(std::uint8_t psm_mode,
                              kRequestedPeriodicTAUT3412 psm_tau,
                              kRequestedActiveTimeT3324 psm_active);
  ActionPtr<IPipeline> SetEdrx(EdrxMode edrx_mode, EdrxActTType act_type,
                               kEDrx edrx_val);
  ActionPtr<IPipeline> SetRai(std::uint8_t rai_mode);
  ActionPtr<IPipeline> SetBandLock(std::uint8_t bl_mode,
                                   std::vector<std::int32_t> const& bands);
  ActionPtr<IPipeline> ResetModemFactory(std::uint8_t res_mode);

  ActionPtr<IPipeline> OpenTcpConnection(
      ActionPtr<OpenNetworkOperation> open_network_operation,
      std::string const& host, std::uint16_t port);

  ActionPtr<IPipeline> OpenUdpConnection(
      ActionPtr<OpenNetworkOperation> open_network_operation,
      std::string const& host, std::uint16_t port);

  ActionPtr<IPipeline> SendData(ConnectionIndex connection,
                                DataBuffer const& data);
  ActionPtr<IPipeline> ReadPacket(ConnectionIndex connection);

  void SetupPoll();
  ActionPtr<IPipeline> Poll();
  void PollEvent(std::int32_t handle, std::string_view flags);

  ActionContext action_context_;
  ModemInit modem_init_;
  std::unique_ptr<ISerialPort> serial_;
  AtSupport at_comm_support_;
  std::set<ConnectionIndex> connections_;
  DataEvent data_event_;
  ActionPtr<RepeatableTask> poll_task_;
  std::unique_ptr<AtListener> poll_listener_;
  OwnActionPtr<ActionsQueue> operation_queue_;
  bool initiated_;
  bool started_;
  int poll_in_queue_ = 0;
};

} /* namespace ae */

#endif  // AETHER_MODEMS_THINGY91X_AT_MODEM_H_
