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

#ifndef AETHER_LORA_MODULES_EBYTE_E22_400_LM_H_
#define AETHER_LORA_MODULES_EBYTE_E22_400_LM_H_

#include "aether/config.h"

#if AE_SUPPORT_LORA && AE_ENABLE_EBYTE_E22_400

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

class EbyteE22LoraModule final : public ILoraModuleDriver {
  static constexpr std::uint16_t kLoraModuleMTU{400};

 public:
  explicit EbyteE22LoraModule(ActionContext action_context,
                              IPoller::ptr const& poller,
                              LoraModuleInit lora_module_init);
  ~EbyteE22LoraModule() override;

  ActionPtr<LoraModuleOperation> Start() override;
  ActionPtr<LoraModuleOperation> Stop() override;
  ActionPtr<OpenNetworkOperation> OpenNetwork(ae::Protocol protocol,
                                              std::string const& host,
                                              std::uint16_t port) override;
  ActionPtr<LoraModuleOperation> CloseNetwork(
      ae::ConnectionLoraIndex connect_index) override;
  ActionPtr<LoraModuleOperation> WritePacket(
      ae::ConnectionLoraIndex connect_index,
      ae::DataBuffer const& data) override;

  DataEvent::Subscriber data_event() override;

  ActionPtr<LoraModuleOperation> SetPowerSaveParam(
      LoraPowerSaveParam const& psp) override;
  ActionPtr<LoraModuleOperation> PowerOff() override;
  ActionPtr<LoraModuleOperation> SetLoraModuleAddress(
      std::uint16_t const& address);  // Module address
  ActionPtr<LoraModuleOperation> SetLoraModuleChannel(
      std::uint8_t const& channel);  // Module channel
  ActionPtr<LoraModuleOperation> SetLoraModuleMode(
      kLoraModuleMode const& mode);  // Module mode
  ActionPtr<LoraModuleOperation> SetLoraModuleLevel(
      kLoraModuleLevel const& level);  // Module level
  ActionPtr<LoraModuleOperation> SetLoraModulePower(
      kLoraModulePower const& power);  // Module power
  ActionPtr<LoraModuleOperation> SetLoraModuleBandWidth(
      kLoraModuleBandWidth const& band_width);  // Module BandWidth
  ActionPtr<LoraModuleOperation> SetLoraModuleCodingRate(
      kLoraModuleCodingRate const& coding_rate);  // Module CodingRate
  ActionPtr<LoraModuleOperation> SetLoraModuleSpreadingFactor(
      kLoraModuleSpreadingFactor const&
          spreading_factor);  // Module spreading factor
  ActionPtr<LoraModuleOperation> SetLoraModuleCRCCheck(
      kLoraModuleCRCCheck const& crc_check);  // Module crc check
  ActionPtr<LoraModuleOperation> SetLoraModuleIQSignalInversion(
      kLoraModuleIQSignalInversion const&
          signal_inversion);  // Module signal inversion

 private:
  void Init();
  void SetupPoll();
  ActionPtr<IPipeline> Poll();
  void PollEvent(std::int32_t handle, std::string_view flags);

  ActionContext action_context_;
  LoraModuleInit lora_module_init_;
  std::unique_ptr<ISerialPort> serial_;
  std::vector<LoraConnection> connect_vec_;
  AtSupport at_comm_support_;
  DataEvent data_event_;
  ActionPtr<RepeatableTask> poll_task_;
  std::unique_ptr<AtListener> poll_listener_;
  OwnActionPtr<ActionsQueue> operation_queue_;
  bool initiated_;
  bool started_;
  bool at_mode_{false};
};

} /* namespace ae */

#endif
#endif  // AETHER_LORA_MODULES_EBYTE_E22_400_LM_H_
