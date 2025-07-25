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

#ifndef AETHER_ADAPTERS_MODEMS_THINGY91X_AT_MODEM_H_
#define AETHER_ADAPTERS_MODEMS_THINGY91X_AT_MODEM_H_

#include <chrono>
#include <memory>

#include "aether/adapters/parent_modem.h"
#include "aether/adapters/modems/serial_ports/iserial_port.h"

namespace ae {

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
  std::uint8_t :1; // start a new byte from bit 1
  std::uint8_t Value : 4;
};

class Thingy91xAtModem : public IModemDriver {
 public:
  explicit Thingy91xAtModem(ModemInit modem_init);
  void Init() override;
  void Start() override;
  void Stop() override;
  void OpenNetwork(std::uint8_t context_index, std::uint8_t connect_index,
                   ae::Protocol protocol, std::string host,
                   std::uint16_t port) override;
  void CloseNetwork(std::uint8_t context_index,
                    std::uint8_t connect_index) override;
  void WritePacket(std::uint8_t connect_index,
                   std::vector<uint8_t> const& data) override;
  void ReadPacket(std::uint8_t connect_index, std::vector<std::uint8_t>& data,
                  std::size_t& size) override;

 private:
  ModemInit modem_init_;
  std::unique_ptr<ISerialPort> serial_;
  ae::Protocol protocol_;
  std::string host_;
  std::uint16_t port_;

  kModemError CheckResponce(std::string responce, std::uint32_t wait_time,
                            std::string error_message);
  kModemError SetBaudRate(std::uint32_t rate);
  kModemError CheckSimStatus();
  kModemError SetupSim(const std::uint8_t pin[4]);
  kModemError SetNetMode(kModemMode modem_mode);
  kModemError SetupNetwork(std::string operator_name, std::string operator_code,
                           std::string apn_name, std::string apn_user,
                           std::string apn_pass, kModemMode modem_mode,
                           kAuthType auth_type);
  kModemError SetupProtoPar();
  kModemError SetPsm(std::int32_t mode, kRequestedPeriodicTAUT3412 tau,
                     kRequestedActiveTimeT3324 active);
  kModemError SetEdrx(EdrxMode mode, EdrxActTType act_type, kEDrx edrx_val);
  kModemError SetRai(std::int8_t mode);
  kModemError SetBandLock(std::int32_t mode,
                          const std::vector<std::int32_t>& bands);
  void sendATCommand(const std::string& command);
  bool waitForResponse(const std::string& expected,
                       std::chrono::milliseconds timeout_ms);
};

} /* namespace ae */

#endif  // AETHER_ADAPTERS_MODEMS_THINGY91X_AT_MODEM_H_