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

#ifndef CLOUD_AETHER_CONSTRUCT_LORA_MODULE_H_
#define CLOUD_AETHER_CONSTRUCT_LORA_MODULE_H_

#include "aether_construct.h"

#if CLOUD_TEST_LORA_MODULE

namespace ae::cloud_test {
static constexpr std::string_view kSerialPortLoraModule =
    "COM1";  // Lora module serial port
SerialInit serial_init_lora_module = {std::string(kSerialPortLoraModule),
                                      kBaudRate::kBaudRate9600};

LoraModulePowerSaveParam psp{
    {kLoraModuleMode::kTransparentTransmission},  // kLoraModuleMode
    {kLoraModuleLevel::kLevel0},                  // kLoraModuleLevel
    {kLoraModulePower::kPower22},                 // kLoraModulePower
    {kLoraModuleBandWidth::kBandWidth125K},       // kLoraModuleBandWidth
    {kLoraModuleCodingRate::kCR4_6},              // kLoraModuleCodingRate
    {kLoraModuleSpreadingFactor::kSF12}           // kLoraModuleSpreadingFactor
};

ae::LoraModuleInit const lora_module_init{
    serial_init_lora_module,                  // Serial port
    psp,                                      // Power Save Parameters
    {kLoraModuleFreqRange::kFREU433},         // Frequency range
    {0},                                      // Lora module address
    {0},                                      // Lora module BS address
    {0},                                      // Channel
    {kLoraModuleCRCCheck::kCRCOff},           // CRC check
    {kLoraModuleIQSignalInversion::kIQoff}};  // Signal inversion

static RcPtr<AetherApp> construct_aether_app() {
  return AetherApp::Construct(
      AetherAppContext{}
#  if defined AE_DISTILLATION
          .AdaptersFactory([](AetherAppContext const& context) {
            auto adapter_registry =
                context.domain().CreateObj<AdapterRegistry>();
            adapter_registry->Add(
                context.domain().CreateObj<ae::LoraModuleAdapter>(
                    ae::GlobalId::kLoraModuleAdapter, context.aether(),
                    context.poller(), lora_module_init));
            return adapter_registry;
          })
#  endif
  );
}
}  // namespace ae::cloud_test

#endif
#endif  // CLOUD_AETHER_CONSTRUCT_LORA_MODULE_H_
