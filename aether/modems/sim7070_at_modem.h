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

#include "aether/config.h"

#if AE_SUPPORT_MODEMS && AE_ENABLE_SIM7070
#  include <set>
#  include <memory>

#  include "aether/poller/poller.h"
#  include "aether/actions/pipeline.h"
#  include "aether/actions/actions_queue.h"
#  include "aether/serial_ports/iserial_port.h"
#  include "aether/serial_ports/at_support/at_support.h"

#  include "aether/modems/imodem_driver.h"

namespace ae {
class Sim7070TcpOpenNetwork;
class Sim7070UdpOpenNetwork;

class Sim7070AtModem final : public IModemDriver {
  friend class Sim7070TcpOpenNetwork;
  friend class Sim7070UdpOpenNetwork;
  static constexpr std::uint16_t kModemMTU{1520};

 public:
  explicit Sim7070AtModem(ActionContext action_context,
                          IPoller::ptr const& poller, ModemInit modem_init);

  ActionPtr<ModemOperation> Start() override;
  ActionPtr<ModemOperation> Stop() override;
  ActionPtr<OpenNetworkOperation> OpenNetwork(Protocol protocol,
                                              std::string const& host,
                                              std::uint16_t port) override;
  ActionPtr<ModemOperation> CloseNetwork(
      ConnectionIndex connect_index) override;
  ActionPtr<WriteOperation> WritePacket(ConnectionIndex connect_index,
                                        DataBuffer const& data) override;

  DataEvent::Subscriber data_event() override;

  ActionPtr<ModemOperation> SetPowerSaveParam(
      ModemPowerSaveParam const& psp) override;
  ActionPtr<ModemOperation> PowerOff() override;

 private:
  void Init();

  ActionPtr<IPipeline> CheckSimStatus();
  ActionPtr<IPipeline> SetupSim(std::uint16_t pin);
  ActionPtr<IPipeline> SetBaudRate(kBaudRate rate);
  ActionPtr<IPipeline> SetNetMode(kModemMode modem_mode);
  ActionPtr<IPipeline> SetupNetwork(std::string const& operator_name,
                                    std::string const& operator_code,
                                    std::string const& apn_name,
                                    std::string const& apn_user,
                                    std::string const& apn_pass,
                                    kModemMode modem_mode, kAuthType auth_type);
  ActionPtr<IPipeline> SetupProtoPar();

  ActionPtr<IPipeline> OpenConnection(
      ActionPtr<OpenNetworkOperation> const& open_network_operation,
      Protocol protocol, std::string const& host, std::uint16_t port);

  ActionPtr<IPipeline> SendData(ConnectionIndex connection,
                                DataBuffer const& data);
  ActionPtr<IPipeline> ReadPacket(ConnectionIndex connection);

  void SetupPoll();
  void PollEvent(std::int32_t handle);

  ActionContext action_context_;
  ModemInit modem_init_;
  std::unique_ptr<ISerialPort> serial_;
  AtSupport at_support_;
  std::set<ConnectionIndex> connections_;
  ConnectionIndex next_connection_index_{0};
  DataEvent data_event_;
  std::unique_ptr<AtListener> poll_listener_;
  OwnActionPtr<ActionsQueue> operation_queue_;
  bool initiated_;
  bool started_;
};

} /* namespace ae */
#endif
#endif  // AETHER_MODEMS_SIM7070_AT_MODEM_H_
