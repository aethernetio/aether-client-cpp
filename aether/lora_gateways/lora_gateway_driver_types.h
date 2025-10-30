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

#ifndef AETHER_LORA_GATEWAYS_LORA_GATEWAY_DRIVER_TYPES_H_
#define AETHER_LORA_GATEWAYS_LORA_GATEWAY_DRIVER_TYPES_H_

#include <string>
#include <vector>

#include "aether/reflect/reflect.h"
#include "aether/serial_ports/serial_port_types.h"

namespace ae {
enum class kLoraGatewayError : std::int8_t {
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

enum class kLoraGatewayMode : std::int8_t {
  kTransparentTransmission = 0,
  kFixedPointTransmission = 1,
  kBroadcastTransmission = 2
};

enum class kLoraGatewayLevel : std::int8_t {
  kLevel0 = 0,
  kLevel1 = 1,
  kLevel2 = 2,
  kLevel3 = 3,
  kLevel4 = 4,
  kLevel5 = 5,
  kLevel6 = 6,
  kLevel7 = 7
};

enum class kLoraGatewayPower : std::int8_t {
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

// Signal Bandwidth 7.81 10.42 15.63 20.83 31.25 41.67 62.5 125 250 500

enum class kLoraGatewayBandWidth : std::int8_t {
  kBandWidth7K81  = 0,
  kBandWidth10K42 = 1,
  kBandWidth15K63 = 2,
  kBandWidth20K83 = 3,
  kBandWidth31K25 = 4,
  kBandWidth41K67 = 5,
  kBandWidth62K5  = 6,
  kBandWidth125K = 7,
  kBandWidth250K = 8,
  kBandWidth500K = 9
};

enum class kLoraGatewayCodingRate : std::int8_t {
  kCR4_5 = 1,
  kCR4_6 = 2,
  kCR4_7 = 3,
  kCR4_8 = 4
};

enum class kLoraGatewaySpreadingFactor : std::int8_t {
  kSF5 = 5,
  kSF6 = 6,
  kSF7 = 7,
  kSF8 = 8,
  kSF9 = 9,
  kSF10 = 10,
  kSF11 = 11,
  kSF12 = 12
};

enum class kLoraGatewayCRCCheck : std::int8_t { kCRCOff = 0, kCRCOn = 1 };

enum class kLoraGatewayIQSignalInversion : std::int8_t {
  kIQoff = 0,
  kIQon = 1,
};

// LoRa uses license-free sub-gigahertz radio frequency bands 
// EU433 (LPD433) or
// CH470 (470-510) in China; 
// EU868 (863–870/873 MHz) in Europe; 
// AU915 (915–928 MHz) in America;
// SA923 (923–928 MHz) in South America;
// US915 (902–928 MHz) in North America; 
// IN865 (865–867 MHz) in India; and 
// AS923 (915–928 MHz) in Asia
enum class kLoraModuleFreqRange : std::int8_t {
  kFREUndef = -1,
  kFREU433 = 0, 
  kFRCH470 = 1,
  kFREU868 = 2,
  kFRAU915 = 3,
  kFRSA923 = 4,
  kFRUS915 = 5,
  kFRIN865 = 6,
  kFRAS923 = 7
};

// ========================lora module init=====================================
struct LoraGatewayPowerSaveParam {
  AE_REFLECT_MEMBERS(lora_gateway_mode, lora_gateway_level, lora_gateway_power,
                     lora_gateway_band_width, lora_gateway_coding_rate,
                     lora_gateway_spreading_factor)
  kLoraGatewayMode lora_gateway_mode{kLoraGatewayMode::kTransparentTransmission};
  kLoraGatewayLevel lora_gateway_level{kLoraGatewayLevel::kLevel0};
  kLoraGatewayPower lora_gateway_power{kLoraGatewayPower::kPower22};
  kLoraGatewayBandWidth lora_gateway_band_width{kLoraGatewayBandWidth::kBandWidth125K};
  kLoraGatewayCodingRate lora_gateway_coding_rate{kLoraGatewayCodingRate::kCR4_6};
  kLoraGatewaySpreadingFactor lora_gateway_spreading_factor{kLoraGatewaySpreadingFactor::kSF12};
};

struct LoraGatewayInit {
  AE_REFLECT_MEMBERS(serial_init, psp, lora_gateway_freq_range, lora_gateway_my_adress,
                     lora_gateway_bs_adress, lora_gateway_channel,
                     lora_gateway_crc_check, lora_gateway_signal_inversion)
  SerialInit serial_init;
  LoraGatewayPowerSaveParam psp;
  kLoraModuleFreqRange lora_gateway_freq_range{kLoraModuleFreqRange::kFREUndef};
  std::uint16_t lora_gateway_my_adress{0};
  std::uint16_t lora_gateway_bs_adress{0};
  std::uint8_t lora_gateway_channel{0};
  kLoraGatewayCRCCheck lora_gateway_crc_check{kLoraGatewayCRCCheck::kCRCOff};
  kLoraGatewayIQSignalInversion lora_gateway_signal_inversion{kLoraGatewayIQSignalInversion::kIQoff};
};

using ConnectionLoraGatewayIndex = std::int8_t;
static constexpr ConnectionLoraGatewayIndex kInvalidConnectionLoraGatewayIndex = -1;

struct LoraGatewayConnection {
  AE_REFLECT_MEMBERS(connect_index, protocol, host, port)
  ConnectionLoraGatewayIndex connect_index;
  ae::Protocol protocol;
  std::string host;
  std::uint16_t port;
};

struct LoraGatewayPacket {
  AE_REFLECT_MEMBERS(connection, length, data, crc)
  LoraGatewayConnection connection;
  std::size_t length{0};
  ae::DataBuffer data;
  std::uint32_t crc{0};
};
}  // namespace ae

#endif  // AETHER_LORA_GATEWAYS_LORA_GATEWAY_DRIVER_TYPES_H_
