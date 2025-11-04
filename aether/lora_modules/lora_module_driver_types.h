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

#ifndef AETHER_LORA_MODULES_LORA_MODULE_DRIVER_TYPES_H_
#define AETHER_LORA_MODULES_LORA_MODULE_DRIVER_TYPES_H_

#include "aether/config.h"

#if AE_SUPPORT_LORA

#  include <string>
#  include <vector>

#  include "aether/reflect/reflect.h"
#  include "aether/serial_ports/serial_port_types.h"

namespace ae {
enum class kLoraModuleError : std::int8_t {
  kNoError = 0,
  kSerialPortError = -1,
  kAtCommandError = -2,
  kBaudRateError = -3,
  kParityError = -4,
  kStopBitsError = -5,
  kLoraAddressError = -6,
  kLoraChannelError = -7,
  kLoraModeError = -8,
  kLoraLevelError = -9,
  kLoraPowerError = -10,
  kLoraBandWidthError = -11,
  kLoraCodingRateError = -12,
  kLoraSFError = -13,
  kLoraCRCError = -14,
  kLoraSIError = -15
};

enum class kLoraModuleMode : std::int8_t {
  kTransparentTransmission = 0,
  kFixedPointTransmission = 1,
  kBroadcastTransmission = 2
};

enum class kLoraModuleLevel : std::int8_t {
  kLevel0 = 0,
  kLevel1 = 1,
  kLevel2 = 2,
  kLevel3 = 3,
  kLevel4 = 4,
  kLevel5 = 5,
  kLevel6 = 6,
  kLevel7 = 7
};

enum class kLoraModulePower : std::int8_t {
  kPower0 = 0,
  kPower1 = 1,
  kPower2 = 2,
  kPower3 = 3,
  kPower4 = 4,
  kPower5 = 5,
  kPower6 = 6,
  kPower7 = 7,
  kPower8 = 8,
  kPower9 = 9,
  kPower10 = 10,
  kPower11 = 11,
  kPower12 = 12,
  kPower13 = 13,
  kPower14 = 14,
  kPower15 = 15,
  kPower16 = 16,
  kPower17 = 17,
  kPower18 = 18,
  kPower19 = 19,
  kPower20 = 20,
  kPower21 = 21,
  kPower22 = 22
};

enum class kLoraModuleBandWidth : std::int8_t { kBandWidth125K = 0 };

enum class kLoraModuleCodingRate : std::int8_t {
  kCR4_5 = 1,
  kCR4_6 = 2,
  kCR4_7 = 3,
  kCR4_8 = 4
};

enum class kLoraModuleSpreadingFactor : std::int8_t {
  kSF5 = 5,
  kSF6 = 6,
  kSF7 = 7,
  kSF8 = 8,
  kSF9 = 9,
  kSF10 = 10,
  kSF11 = 11,
  kSF12 = 12
};

enum class kLoraModuleCRCCheck : std::int8_t { kCRCOff = 0, kCRCOn = 1 };

enum class kLoraModuleIQSignalInversion : std::int8_t {
  kIQoff = 0,
  kIQon = 1,
};

// ========================lora module init=====================================
struct LoraPowerSaveParam {
  AE_REFLECT_MEMBERS(lora_module_mode, lora_module_level, lora_module_power,
                     lora_module_band_width, lora_module_coding_rate,
                     lora_module_spreading_factor)
  kLoraModuleMode lora_module_mode{kLoraModuleMode::kTransparentTransmission};
  kLoraModuleLevel lora_module_level{kLoraModuleLevel::kLevel0};
  kLoraModulePower lora_module_power{kLoraModulePower::kPower22};
  kLoraModuleBandWidth lora_module_band_width{
      kLoraModuleBandWidth::kBandWidth125K};
  kLoraModuleCodingRate lora_module_coding_rate{kLoraModuleCodingRate::kCR4_6};
  kLoraModuleSpreadingFactor lora_module_spreading_factor{
      kLoraModuleSpreadingFactor::kSF12};
};

struct LoraModuleInit {
  AE_REFLECT_MEMBERS(serial_init, psp, lora_module_my_adress,
                     lora_module_bs_adress, lora_module_channel,
                     lora_module_crc_check, lora_module_signal_inversion)
  SerialInit serial_init;
  LoraPowerSaveParam psp;
  std::uint16_t lora_module_my_adress{0};
  std::uint16_t lora_module_bs_adress{0};
  std::uint8_t lora_module_channel{0};
  kLoraModuleCRCCheck lora_module_crc_check{kLoraModuleCRCCheck::kCRCOff};
  kLoraModuleIQSignalInversion lora_module_signal_inversion{
      kLoraModuleIQSignalInversion::kIQoff};
};

using ConnectionLoraIndex = std::int8_t;
static constexpr ConnectionLoraIndex kInvalidConnectionLoraIndex = -1;

struct LoraConnection {
  AE_REFLECT_MEMBERS(connect_index, protocol, host, port)
  ConnectionLoraIndex connect_index;
  ae::Protocol protocol;
  std::string host;
  std::uint16_t port;
};

struct LoraPacket {
  AE_REFLECT_MEMBERS(connection, length, data, crc)
  LoraConnection connection;
  std::size_t length{0};
  ae::DataBuffer data;
  std::uint32_t crc{0};
};
}  // namespace ae

#endif
#endif  // AETHER_LORA_MODULES_LORA_MODULE_DRIVER_TYPES_H_
