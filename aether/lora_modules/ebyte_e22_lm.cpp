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

#include "aether/lora_modules/ebyte_e22_lm.h"
#if AE_SUPPORT_LORA && AE_ENABLE_EBYTE_E22_LM

#  include <bitset>
#  include <string_view>

#  include "aether/misc/defer.h"
#  include "aether/misc/from_chars.h"
#  include "aether/actions/pipeline.h"
#  include "aether/actions/gen_action.h"
#  include "aether/mstream_buffers.h"
#  include "aether/serial_ports/serial_port_factory.h"

#  include "aether/lora_modules/lora_modules_tele.h"

namespace ae {
static constexpr Duration kOneSecond = std::chrono::milliseconds{1000};
static constexpr Duration kTwoSeconds = std::chrono::milliseconds{2000};
static constexpr Duration kTenSeconds = std::chrono::milliseconds{10000};

EbyteE22LoraModule::EbyteE22LoraModule(ActionContext action_context,
                                       IPoller::ptr const& poller,
                                       LoraModuleInit lora_module_init)
    : action_context_{action_context},
      lora_module_init_{std::move(lora_module_init)},
      serial_{SerialPortFactory::CreatePort(action_context_, std::move(poller),
                                            lora_module_init_.serial_init)},
      at_comm_support_{action_context_, *serial_},
      operation_queue_{action_context_},
      initiated_{false},
      started_{false} {
  Init();
  Start();
}

EbyteE22LoraModule::~EbyteE22LoraModule() { Stop(); }

ActionPtr<EbyteE22LoraModule::LoraModuleOperation> EbyteE22LoraModule::Start() {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation> EbyteE22LoraModule::Stop() {
  return {};
}

ActionPtr<EbyteE22LoraModule::OpenNetworkOperation>
EbyteE22LoraModule::OpenNetwork(ae::Protocol /* protocol*/,
                                std::string const& /*host*/,
                                std::uint16_t /* port*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::CloseNetwork(ae::ConnectionLoraIndex /*connect_index*/) {
  return {};
}
// void EbyteE22LoraModule::CloseNetwork(
//    ae::ConnectionLoraIndex /*connect_index*/){};

ActionPtr<EbyteE22LoraModule::WriteOperation>
EbyteE22LoraModule::WritePacket(ae::ConnectionLoraIndex /*connect_index*/,
                                   ae::DataBuffer const& /*data*/) {
  return {};
}
// void EbyteE22LoraModule::WritePacket(ae::ConnectionLoraIndex
// /*connect_index*/,
//                                      ae::DataBuffer const& /*data*/) {};

ActionPtr<IPipeline> EbyteE22LoraModule::ReadPacket(
    ConnectionLoraIndex /* connection */) {
  return {};
}
// DataBuffer EbyteE22LoraModule::ReadPacket(
//     ae::ConnectionLoraIndex /*connect_index*/, ae::Duration /*timeout*/) {
//   DataBuffer data{};
//
//   return data;
// };

EbyteE22LoraModule::DataEvent::Subscriber
EbyteE22LoraModule::data_event() {
  return EventSubscriber{data_event_};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetPowerSaveParam(LoraPowerSaveParam const& /*psp*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::PowerOff() {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetLoraModuleAddress(std::uint16_t const& /*address*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetLoraModuleChannel(std::uint8_t const& /*channel*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetLoraModuleMode(kLoraModuleMode const& /*mode*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetLoraModuleLevel(kLoraModuleLevel const& /*level*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetLoraModulePower(kLoraModulePower const& /*power*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetLoraModuleBandWidth(
    kLoraModuleBandWidth const& /*band_width*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetLoraModuleCodingRate(
    kLoraModuleCodingRate const& /*coding_rate*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetLoraModuleSpreadingFactor(
    kLoraModuleSpreadingFactor const& /*spreading_factor*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetLoraModuleCRCCheck(
    kLoraModuleCRCCheck const& /*crc_check*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetLoraModuleIQSignalInversion(
    kLoraModuleIQSignalInversion const& /*signal_inversion*/) {
  return {};
}

void EbyteE22LoraModule::Init() {}

}  // namespace ae
#endif
