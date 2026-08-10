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

#ifndef EXAMPLES_COMMON_AETHER_CONSTRUCT_LORA_MODULE_H_
#define EXAMPLES_COMMON_AETHER_CONSTRUCT_LORA_MODULE_H_

#include "aether/config.h"
#include "aether_construct.h"

#if AE_EXAMPLE_LORA_MODULE
#  if !AE_SUPPORT_LORA
#    error "Lora module is not supported"
#  else

namespace ae::examples {
static constexpr std::string_view kSerialPortLoraModule = "COM2";
SerialInit serial_init_lora_module = {std::string(kSerialPortLoraModule),
                                      kBaudRate::kBaudRate9600};

LoraPowerSaveParam psp{kLoraModuleMode::kTransparentTransmission,
                       kLoraModuleLevel::kLevel0,
                       kLoraModulePower::kPower22,
                       kLoraModuleBandWidth::kBandWidth125K,
                       kLoraModuleCodingRate::kCR4_6,
                       kLoraModuleSpreadingFactor::kSF12};

ae::LoraModuleInit const lora_module_init{serial_init_lora_module,
                                          psp,
                                          kLoraModuleFreqRange::kFREUndef,
                                          0,
                                          0,
                                          0,
                                          kLoraModuleCRCCheck::kCRCOff,
                                          kLoraModuleIQSignalInversion::kIQoff};

static std::unique_ptr<AetherApp> construct_aether_app() {
  return AetherApp::Construct(
      AetherAppContext{}
#    if defined AE_DISTILLATION
          .AddAdapterFactory([](AetherAppContext const& context) {
            return LoraModuleAdapter::ptr::Create(
                CreateWith{context.domain()}.with_id(
                    ae::GlobalId::kLoraModuleAdapter),
                context.aether(), context.poller(), lora_module_init);
          })
#    endif
  );
}
}  // namespace ae::examples

#  endif
#endif

#endif  // EXAMPLES_COMMON_AETHER_CONSTRUCT_LORA_MODULE_H_
