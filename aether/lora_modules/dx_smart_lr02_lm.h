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

#ifndef AETHER_LORA_MODULES_DX_SMART_LR02_LM_H_
#define AETHER_LORA_MODULES_DX_SMART_LR02_LM_H_

#include "aether/config.h"

#if AE_SUPPORT_LORA && AE_ENABLE_DX_SMART_LR02_LM
#  include <set>
#  include <memory>

#  include "aether/poller/poller.h"
#  include "aether/actions/pipeline.h"
#  include "aether/actions/actions_queue.h"
#  include "aether/actions/repeatable_task.h"
#  include "aether/serial_ports/iserial_port.h"
#  include "aether/serial_ports/at_support/at_support.h"

#  include "aether/lora_modules/ilora_module_driver.h"

namespace ae {
class DxSmartLr02TcpOpenNetwork;
class DxSmartLr02UdpOpenNetwork;

static const std::map<kBaudRate, std::string> baud_rate_commands_lr02 = {
    {kBaudRate::kBaudRate1200, "AT+BAUD1"},
    {kBaudRate::kBaudRate2400, "AT+BAUD2"},
    {kBaudRate::kBaudRate4800, "AT+BAUD3"},
    {kBaudRate::kBaudRate9600, "AT+BAUD4"},
    {kBaudRate::kBaudRate19200, "AT+BAUD5"},
    {kBaudRate::kBaudRate38400, "AT+BAUD6"},
    {kBaudRate::kBaudRate57600, "AT+BAUD7"},
    {kBaudRate::kBaudRate115200, "AT+BAUD8"},
    {kBaudRate::kBaudRate128000, "AT+BAUD9"}};

class DxSmartLr02LoraModule final : public ILoraModuleDriver {
  friend class DxSmartLr02TcpOpenNetwork;
  friend class DxSmartLr02UdpOpenNetwork;
  static constexpr std::uint16_t kLoraModuleMTU{400};

 public:
  explicit DxSmartLr02LoraModule(ActionContext action_context,
                                 IPoller::ptr const& poller,
                                 LoraModuleInit lora_module_init);
  ~DxSmartLr02LoraModule() override;

  ActionPtr<LoraModuleOperation> Start() override;
  ActionPtr<LoraModuleOperation> Stop() override;
  ActionPtr<OpenNetworkOperation> OpenNetwork(Protocol protocol,
                                              std::string const& host,
                                              std::uint16_t port) override;
  ActionPtr<LoraModuleOperation> CloseNetwork(
      ConnectionLoraIndex connect_index) override;
  ActionPtr<WriteOperation> WritePacket(ConnectionLoraIndex connect_index,
                                        ae::DataBuffer const& data) override;

  DataEvent::Subscriber data_event() override;

  ActionPtr<LoraModuleOperation> SetPowerSaveParam(
      LoraPowerSaveParam const& psp) override;
  ActionPtr<LoraModuleOperation> PowerOff() override;
  ActionPtr<LoraModuleOperation> SetLoraModuleAddress(
      std::uint16_t const& address);  // Module address
  ActionPtr<LoraModuleOperation> SetLoraModuleChannel(
      std::uint8_t const& channel);  // Module channel

  ActionPtr<LoraModuleOperation> SetLoraModuleCRCCheck(
      kLoraModuleCRCCheck const& crc_check);  // Module crc check
  ActionPtr<LoraModuleOperation> SetLoraModuleIQSignalInversion(
      kLoraModuleIQSignalInversion const&
          signal_inversion);  // Module signal inversion

 private:
  void Init();

  ActionPtr<IPipeline> OpenTcpConnection(
      ActionPtr<OpenNetworkOperation> open_network_operation,
      std::string const& host, std::uint16_t port);

  ActionPtr<IPipeline> OpenUdpConnection(
      ActionPtr<OpenNetworkOperation> open_network_operation,
      std::string const& host, std::uint16_t port);

  ActionPtr<IPipeline> SendData(ConnectionLoraIndex connection,
                                DataBuffer const& data);
  ActionPtr<IPipeline> ReadPacket(ConnectionLoraIndex connection);

  void SetupPoll();
  ActionPtr<IPipeline> Poll();
  void PollEvent(std::int32_t handle, std::string_view flags);

  ActionContext action_context_;
  LoraModuleInit lora_module_init_;
  std::unique_ptr<ISerialPort> serial_;
  std::set<ConnectionLoraIndex> connections_;
  AtSupport at_comm_support_;
  DataEvent data_event_;
  ActionPtr<RepeatableTask> poll_task_;
  std::unique_ptr<AtListener> poll_listener_;
  OwnActionPtr<ActionsQueue> operation_queue_;
  bool initiated_;
  bool started_;
  bool at_mode_{false};

  ActionPtr<IPipeline> EnterAtMode();
  ActionPtr<IPipeline> ExitAtMode();

  ActionPtr<IPipeline> SetLoraModuleMode(
      kLoraModuleMode const& mode);  // Module mode
  ActionPtr<IPipeline> SetLoraModuleLevel(
      kLoraModuleLevel const& level);  // Module level
  ActionPtr<IPipeline> SetLoraModulePower(
      kLoraModulePower const& power);  // Module power
  ActionPtr<IPipeline> SetLoraModuleBandWidth(
      kLoraModuleBandWidth const& band_width);  // Module BandWidth
  ActionPtr<IPipeline> SetLoraModuleCodingRate(
      kLoraModuleCodingRate const& coding_rate);  // Module CodingRate
  ActionPtr<IPipeline> SetLoraModuleSpreadingFactor(
      kLoraModuleSpreadingFactor const&
          spreading_factor);  // Module spreading factor
  ActionPtr<IPipeline> SetupSerialPort(SerialInit& serial_init);
  ActionPtr<IPipeline> SetBaudRate(kBaudRate baud_rate);
  ActionPtr<IPipeline> SetParity(kParity parity);
  ActionPtr<IPipeline> SetStopBits(kStopBits stop_bits);

  ActionPtr<IPipeline> SetupLoraNet(LoraModuleInit& lora_module_init);

  std::string AdressToString(uint16_t value);
  std::string ChannelToString(uint8_t value);
};

} /* namespace ae */
#endif
#endif  // AETHER_LORA_MODULES_DX_SMART_LR02_LM_H_
