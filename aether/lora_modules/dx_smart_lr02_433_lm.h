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

#ifndef AETHER_LORA_MODULES_DX_SMART_LR02_433_LM_H_
#define AETHER_LORA_MODULES_DX_SMART_LR02_433_LM_H_

#include <memory>

#include "aether/lora_modules/ilora_module_driver.h"
#include "aether/adapters/lora_module_adapter.h"
#include "aether/serial_ports/iserial_port.h"
#include "aether/serial_ports/at_comm_support.h"

namespace ae {

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
 public:
  explicit DxSmartLr02LoraModule(LoraModuleAdapter& adapter, IPoller::ptr poller,
                                 LoraModuleInit lora_module_init, Domain* domain);
  ~DxSmartLr02LoraModule() override;

  ActionPtr<LoraModuleOperation> Init() override;
  ActionPtr<LoraModuleOperation> Start() override;
  ActionPtr<LoraModuleOperation> Stop() override;
  ActionPtr<OpenNetworkOperation> OpenNetwork(ae::Protocol protocol,
                                  std::string const& host,
                                  std::uint16_t port) override;
  ActionPtr<LoraModuleOperation> CloseNetwork(ae::ConnectionLoraIndex connect_index) override;
  ActionPtr<LoraModuleOperation> WritePacket(ae::ConnectionLoraIndex connect_index,
                   ae::DataBuffer const& data) override;
  
  DataEvent::Subscriber data_event() override;
  
  ActionPtr<LoraModuleOperation> SetPowerSaveParam(std::string const& psp) override;
  ActionPtr<LoraModuleOperation> PowerOff() override;
  ActionPtr<LoraModuleOperation> SetLoraModuleAddress(std::uint16_t const& address);  // Module address
  ActionPtr<LoraModuleOperation> SetLoraModuleChannel(std::uint8_t const& channel);   // Module channel
  ActionPtr<LoraModuleOperation> SetLoraModuleMode(kLoraModuleMode const& mode);      // Module mode
  ActionPtr<LoraModuleOperation> SetLoraModuleLevel(kLoraModuleLevel const& level);   // Module level
  ActionPtr<LoraModuleOperation> SetLoraModulePower(kLoraModulePower const& power);   // Module power
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
  ActionContext action_context_;
  LoraModuleInit lora_module_init_;
  std::unique_ptr<ISerialPort> serial_;
  std::vector<LoraConnection> connect_vec_;
  std::unique_ptr<AtCommSupport> at_comm_support_;
  bool at_mode_{false};

  static constexpr std::uint16_t kLoraModuleMTU{400};

  kLoraModuleError CheckResponse(std::string const& response,
                                 std::uint32_t const wait_time,
                                 std::string const& error_message);

  kLoraModuleError EnterAtMode();
  kLoraModuleError ExitAtMode();

  kLoraModuleError SetupSerialPort(SerialInit& serial_init);
  kLoraModuleError SetBaudRate(kBaudRate baud_rate);
  kLoraModuleError SetParity(kParity parity);
  kLoraModuleError SetStopBits(kStopBits stop_bits);

  kLoraModuleError SetupLoraNet(LoraModuleInit& lora_module_init);

  std::string AdressToString(uint16_t value);
  std::string ChannelToString(uint8_t value);
};

} /* namespace ae */

#endif  // AETHER_LORA_MODULES_DX_SMART_LR02_433_LM_H_
