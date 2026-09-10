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

/**
 * @file modem_driver_types.h
 * @brief Shared modem configuration, radio settings, and connection
 * identifiers.
 */

#ifndef AETHER_MODEMS_MODEM_DRIVER_TYPES_H_
#define AETHER_MODEMS_MODEM_DRIVER_TYPES_H_

#include "aether/config.h"
#if AE_SUPPORT_MODEMS
#  include <string>
#  include <vector>

#  include "aether-miscpp/reflect/reflect.h"
#  include "aether/serial_ports/serial_port_types.h"

namespace ae {
/**
 * @brief Named error values used by modem configuration and conversion code.
 */
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
  kXDataMode = -25,
  kDataLength = -26
};

/**
 * @brief Requested radio access mode; support depends on the modem.
 */
enum class kModemMode : std::uint8_t {
  kModeAuto = 0,
  kModeGSMOnly = 1,
  kModeLTEOnly = 2,
  kModeGSMLTE = 3,
  kModeCatM = 4,
  kModeNbIot = 5,
  kModeCatMNbIot = 6
};

/**
 * @brief Packet data authentication method.
 */
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

/**
 * @brief Packed requested active-time timer with a five-bit value and unit
 * selector.
 */
struct kRequestedActiveTimeT3324 {
  AE_REFLECT_MEMBERS(byte)
  union {
    std::uint8_t byte;
    struct {
      std::uint8_t Value : 5;       // Lower 5 bits (0-4)
      std::uint8_t Multiplier : 3;  // Upper 3 bits (5-7)
    } bits;
  };

  /// @brief Pack the supplied values after masking them to the stored fields.
  kRequestedActiveTimeT3324(std::uint8_t val = 0, std::uint8_t mult = 0) {
    bits.Value = (val & 0x1F);
    bits.Multiplier = (mult & 0x07);
  }

  /// @brief Return the packed timer byte.
  operator std::uint8_t() const { return byte; }
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

/**
 * @brief Packed requested periodic tracking-area-update timer.
 */
struct kRequestedPeriodicTAUT3412 {
  AE_REFLECT_MEMBERS(byte)
  union {
    std::uint8_t byte;
    struct {
      std::uint8_t Value : 5;       // Lower 5 bits (0-4)
      std::uint8_t Multiplier : 3;  // Upper 3 bits (5-7)
    } bits;
  };

  /// @brief Pack the supplied values after masking them to the stored fields.
  kRequestedPeriodicTAUT3412(std::uint8_t val = 0, std::uint8_t mult = 0) {
    bits.Value = (val & 0x1F);
    bits.Multiplier = (mult & 0x03);
  }

  /// @brief Return the packed timer byte.
  operator std::uint8_t() const { return byte; }
};

/**
 * @brief eDRX configuration mode passed to the modem.
 */
enum class EdrxMode : std::uint8_t {
  kEdrxDisable = 0,
  kEdrxEnable = 1,
  kEdrxEnableCode = 2,
  kEdrxDisableCode = 3
};

/**
 * @brief Radio access technology selector for eDRX.
 */
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

/**
 * @brief Packed requested/provided eDRX values and paging time window.
 */
struct kEDrx {
  AE_REFLECT_MEMBERS(bytes)
  union {
    struct {
      AE_REFLECT_MEMBERS(byte0, byte1)
      std::uint8_t byte0;  // First byte (ReqEDRXValue + ProvEDRXValue)
      std::uint8_t byte1;  // Second byte (PTWValue + padding)
    } bytes;

    struct {
      std::uint8_t ReqEDRXValue : 4;   // The lower 4 bits of the first byte
      std::uint8_t ProvEDRXValue : 4;  // The upper 4 bits of the first byte
      std::uint8_t PTWValue : 4;       // The lower 4 bits of the second byte
      std::uint8_t : 4;  // Placeholder (upper 4 bits of the second byte)
    } bits;
  };

  /// @brief Pack the supplied values after masking them to the stored fields.
  kEDrx(std::uint8_t r_edrx = 0, std::uint8_t p_edrx = 0,
        std::uint8_t ptw_val = 0) {
    bits.ReqEDRXValue = (r_edrx & 0x0F);
    bits.ProvEDRXValue = (p_edrx & 0x0F);
    bits.PTWValue = (ptw_val & 0x0F);
  }

  /// @brief Return byte0 in the high byte and byte1 in the low byte.
  operator std::uint16_t() const {
    return static_cast<std::uint16_t>((bytes.byte0 << 8) | (bytes.byte1 << 0));
  }
};

/**
 * @brief Symbolic band identifiers; enum values are not physical band numbers.
 */
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

/**
 * @brief Requested power setting for one radio band.
 */
struct BandPower {
  AE_REFLECT_MEMBERS(band, power)
  kModemBand band;     ///< Radio band to configure.
  std::uint8_t power;  ///< Driver-specific transmit-power value.
};

// ========================modem init========================================
/**
 * @brief Requested power-saving and radio settings.
 * @note Individual drivers may support only a subset or ignore these settings.
 */
struct ModemPowerSaveParam {
  AE_REFLECT_MEMBERS(psm_mode, tau, act, edrx_mode, act_type, edrx_val,
                     rai_mode, bands_mode, bands, modem_mode, power)
  std::uint8_t psm_mode;  ///< Requested power-saving mode.
  kRequestedPeriodicTAUT3412
      tau;  ///< Requested periodic tracking-area-update timer.
  kRequestedActiveTimeT3324 act;  ///< Requested active-time timer.
  EdrxMode edrx_mode;             ///< Requested eDRX mode.
  EdrxActTType act_type;          ///< Radio access type for eDRX.
  kEDrx edrx_val;           ///< Requested and provided eDRX timing values.
  std::uint8_t rai_mode;    ///< Requested release assistance indication mode.
  std::uint8_t bands_mode;  ///< Driver-specific band-selection mode.
  std::vector<std::int32_t>
      bands;  ///< Requested band identifiers for the selected driver.
  kModemMode modem_mode;  ///< Requested radio access mode.
  std::vector<BandPower>
      power;  ///< Requested per-band transmit-power settings.
};

/**
 * @brief Base-station information carried in modem configuration.
 */
struct ModemBaseStation {
  AE_REFLECT_MEMBERS(cell_identifier)
  std::uint32_t cell_identifier;  ///< Cell identifier, when available.
};

/**
 * @brief Serial, SIM, network, authentication, and power configuration.
 * @note Field support depends on the driver. Supplying credentials or an SSL
 * certificate does not imply that the driver implements the corresponding
 * feature.
 */
struct ModemInit {
  AE_REFLECT_MEMBERS(serial_init, psp, bs, pin, use_pin, operator_code,
                     operator_name, apn_name, apn_user, apn_pass, modem_mode,
                     auth_type, use_auth, auth_user, auth_pass, ssl_cert,
                     use_ssl)
  SerialInit serial_init;     ///< Platform serial port configuration.
  ModemPowerSaveParam psp;    ///< Requested power-saving configuration.
  ModemBaseStation bs;        ///< Base-station configuration data.
  std::uint16_t pin;          ///< Numeric SIM PIN used when use_pin is enabled.
  bool use_pin;               ///< Whether to submit the configured SIM PIN.
  kModemMode modem_mode;      ///< Requested radio access mode.
  std::string operator_code;  ///< Numeric operator code; an empty value permits
                              ///< automatic selection.
  std::string operator_name;  ///< Operator name, preferred over operator_code
                              ///< when supported.
  std::string apn_name;       ///< Access point name for packet data.
  std::string apn_user;       ///< APN authentication user name.
  std::string apn_pass;       ///< APN authentication password.
  kAuthType auth_type;        ///< Requested APN authentication method.
  bool use_auth;  ///< Optional authentication flag; support is driver-specific.
  std::string auth_user;  ///< Optional authentication user name.
  std::string auth_pass;  ///< Optional authentication password.
  std::string ssl_cert;   ///< Optional SSL certificate configuration.
  bool use_ssl;           ///< Optional SSL flag; support is driver-specific.
};

/**
 * @brief Signed modem-local socket identifier.
 */
using ConnectionIndex = std::int8_t;
/**
 * @brief Sentinel for an absent or unopened modem connection.
 */
static constexpr ConnectionIndex kInvalidConnectionIndex = -1;
}  // namespace ae
#endif
#endif  // AETHER_MODEMS_MODEM_DRIVER_TYPES_H_
