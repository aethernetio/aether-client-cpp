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

#ifndef AETHER_MODEMS_MODEM_DRIVER_TYPES_H_
#define AETHER_MODEMS_MODEM_DRIVER_TYPES_H_

#include <string>
#include <vector>

#include "aether/reflect/reflect.h"
#include "aether/serial_ports/serial_port_types.h"

namespace ae {
enum class kModemError : std::int8_t {
  kNoError = 0,
  kSerialPortError = -1,
  kAtCommandError = -2,
  kBaudRateError = -3,
  kDbmaToHexBand = -4,
  kDbmaToHexRange = -5,
  kHexToDbmaBand = -6,
  kHexToDbmaRange = -7,
  kSetTxPowerBand = -8,
  kGetTxPowerBand = -9,
  kCheckSimStatus = -10,
  kPinWrong = -11,
  kSetupSim = -12,
  kSetNetMode = -13,
  kSetNetwork = -14,
  kOpenConnection = -15,
  kResetMode = -16,
  kSetRai = -17,
  kSetBandLock = -18,
  kSetPsm = -19,
  kSetPwr = -20,
  kConnectIndex = -21,
  kPPEMiss0x = -22,
  kPPEInvalidHex = -23,
  kPPEFailedConvert = -24,
  kXDataMode = -25
};

enum class kModemMode : std::uint8_t {
  kModeAuto = 0,
  kModeGSMOnly = 1,
  kModeLTEOnly = 2,
  kModeGSMLTE = 3,
  kModeCatM = 4,
  kModeNbIot = 5,
  kModeCatMNbIot = 6
};

enum class kAuthType : std::uint8_t {
  kAuthTypeNone = 0,
  kAuthTypePAP = 1,
  kAuthTypeCHAP = 2,
  kAuthTypePAPCHAP = 3
};

// ========================power save=====================================

// Multiplier Bits
// 8 7 6
// 0 0 0 – Value is incremented in multiples of 2 s
// 0 0 1 – Value is incremented in multiples of 1 min
// 0 1 0 – Value is incremented in multiples of 6 min
// 1 1 1 – Value indicates that the timer is deactivated

struct kRequestedActiveTimeT3324 {
  std::uint8_t Value : 5;
  std::uint8_t Multiplier : 3;
};

// Bits 8 to 6 define the timer value unit for the General Packet Radio Services
// (GPRS) timer as follows:
// Multiplier Bits
// 8 7 6
// 0 0 0 – Value is incremented in multiples of 10 min
// 0 0 1 – Value is incremented in multiples of 1 h
// 0 1 0 – Value is incremented in multiples of 10 h
// 0 1 1 – Value is incremented in multiples of 2 s
// 1 0 0 – Value is incremented in multiples of 30 s
// 1 0 1 – Value is incremented in multiples of 1 min
// 1 1 0 – Value is incremented in multiples of 320 h

struct kRequestedPeriodicTAUT3412 {
  std::uint8_t Value : 5;
  std::uint8_t Multiplier : 3;
};

enum class EdrxMode : std::uint8_t {
  kEdrxDisable = 0,
  kEdrxEnable = 1,
  kEdrxEnableCode = 2,
  kEdrxDisableCode = 3
};

enum class EdrxActTType : std::uint8_t {
  kEdrxActDisable = 0,
  kEdrxActEUtranWBS1 = 4,
  kEdrxActEUtranNBS1 = 5
};

// eDRX_value Bits
// 4 3 2 1 – E-UTRAN eDRX cycle length duration
// 0 0 0 0 – 5.12 s2
// 0 0 0 1 – 10.24 s2
// 0 0 1 0 – 20.48 s
// 0 0 1 1 – 40.96 s
// 0 1 0 0 – 61.44 s3
// 0 1 0 1 – 81.92 s
// 0 1 1 0 – 102.4 s3
// 0 1 1 1 – 122.88 s3
// 1 0 0 0 – 143.36 s3
// 1 0 0 1 – 163.84 s
// 1 0 1 0 – 327.68 s
// 1 0 1 1 – 655,36 s
// 1 1 0 0 – 1310.72 s
// 1 1 0 1 – 2621.44 s
// 1 1 1 0 – 5242.88 s4
// 1 1 1 1 – 10485.76 s4

struct kEDrx {
  std::uint8_t ReqEDRXValue : 4;
  std::uint8_t ProvEDRXValue : 4;
  std::uint8_t PTWValue : 4;
};

enum class kModemBand : std::uint8_t {
  kWCDMA_B1 = 0,
  kWCDMA_B2 = 1,
  kWCDMA_B4 = 2,
  kWCDMA_B5 = 3,
  kWCDMA_B8 = 4,
  kLTE_B1 = 5,
  kLTE_B2 = 6,
  kLTE_B3 = 7,
  kLTE_B4 = 8,
  kLTE_B5 = 9,
  kLTE_B7 = 10,
  kLTE_B8 = 11,
  kLTE_B12 = 12,
  kLTE_B13 = 13,
  kLTE_B17 = 14,
  kLTE_B18 = 15,
  kLTE_B19 = 16,
  kLTE_B20 = 17,
  kLTE_B25 = 18,
  kLTE_B26 = 19,
  kLTE_B28 = 20,
  kLTE_B38 = 21,
  kLTE_B39 = 22,
  kLTE_B40 = 23,
  kLTE_B41 = 24,
  kTDS_B34 = 25,
  kTDS_B39 = 26,
  kGSM_850 = 27,
  kGSM_900 = 28,
  kGSM_1800 = 29,
  kGSM_1900 = 30,
  kTDSCDMA_B34 = 31,
  kTDSCDMA_B39 = 32,
  kALL_BAND = 33,
  kINVALID_BAND = 34
};

struct BandPower {
  kModemBand band;
  std::uint8_t power;
};

struct kPowerSaveParam {
  std::uint8_t psm_mode;
  kRequestedPeriodicTAUT3412 tau;
  kRequestedActiveTimeT3324 act;
  EdrxMode edrx_mode;
  EdrxActTType act_type;
  kEDrx edrx_val;
  std::uint8_t rai_mode;
  std::uint8_t bands_mode;
  std::vector<std::int32_t> bands;
  kModemMode modem_mode;
  std::vector<BandPower> power;
};

// ========================modem init==========================================
struct ModemInit {
  AE_REFLECT_MEMBERS(serial_init, pin, use_pin, operator_name, apn_name,
                     apn_user, apn_pass, modem_mode, auth_type, use_auth,
                     auth_user, auth_pass, ssl_cert, use_ssl)
  SerialInit serial_init;
  std::uint8_t pin[4];
  bool use_pin;
  kModemMode modem_mode;
  std::string operator_code;
  std::string operator_name;
  std::string apn_name;
  std::string apn_user;
  std::string apn_pass;
  kAuthType auth_type;
  bool use_auth;
  std::string auth_user;
  std::string auth_pass;
  std::string ssl_cert;
  bool use_ssl;
};

/**
 * @enum PollEvents
 * @brief Represents event flags for poll() system call monitoring
 *
 * Each flag corresponds to a specific type of I/O event that can be monitored
 * using the poll() system call. Flags can be combined using bitwise OR
 * operations.
 */
enum class PollEvents : std::uint8_t {
  kPOLLIN = 0x0001,
  kPOLLPRI = 0x0002,
  kPOLLOUT = 0x0004,
  kPOLLERR = 0x0008,
  kPOLLHUP = 0x0010,
  kPOLLNVAL = 0x0020
};

using ConnectionIndex = std::int8_t;
static constexpr ConnectionIndex kInvalidConnectionIndex = -1;

struct PollResult {
  ConnectionIndex connect_index;
  std::vector<PollEvents> revents;
};

}  // namespace ae

#endif  // AETHER_MODEMS_MODEM_DRIVER_TYPES_H_
