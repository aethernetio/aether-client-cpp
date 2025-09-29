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

#include "aether/lora_modules/dx_smart_lr02_433_lm.h"
#include "aether/serial_ports/serial_port_factory.h"
#include "aether/mstream.h"
#include "aether/mstream_buffers.h"
#include "aether/transport/data_packet_collector.h"

#include "aether/lora_modules/lora_modules_tele.h"

namespace ae {

DxSmartLr02LoraModule::DxSmartLr02LoraModule(LoraModuleAdapter& adapter,
                                             IPoller::ptr poller,
                                             LoraModuleInit lora_module_init,
                                             Domain* domain)
    : ILoraModuleDriver{std::move(poller), std::move(lora_module_init), domain},
      adapter_{&adapter} {
  serial_ = SerialPortFactory::CreatePort(*adapter_->aether_.as<Aether>(),
                                          adapter_->poller_,
                                          GetLoraModuleInit().serial_init);
  at_comm_support_ = std::make_unique<AtCommSupport>(serial_.get());
};

bool DxSmartLr02LoraModule::Init() {
  LoraModuleInit lora_module_init = GetLoraModuleInit();
  kLoraModuleError err{kLoraModuleError::kNoError};

  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open.");
    return false;
  }

  err = EnterAtMode();

  if (err != kLoraModuleError::kNoError) {
    AE_TELED_ERROR("Can't enter to the AT command mode.");
    return false;
  }

  at_comm_support_->SendATCommand("AT");  // Checking the connection
  if (err = CheckResponse("OK", 1000, "AT command error!");
      err != kLoraModuleError::kNoError) {
    AE_TELED_ERROR("AT command error {}", err);
    return false;
  }

  if (err = SetupSerialPort(lora_module_init.serial_init);
      err != kLoraModuleError::kNoError) {
    AE_TELED_ERROR("Setup module serial port error {}", err);
    return false;
  }

  if (err = SetupLoraNet(lora_module_init); err != kLoraModuleError::kNoError) {
    AE_TELED_ERROR("Setup module serial port error {}", err);
    return false;
  }

  err = ExitAtMode();

  if (err != kLoraModuleError::kNoError) {
    AE_TELED_ERROR("Can't exit from the AT command mode.");
    return false;
  }

  return true;
};

bool DxSmartLr02LoraModule::Start() {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }

  return true;
};

bool DxSmartLr02LoraModule::Stop() {
  kLoraModuleError err{kLoraModuleError::kNoError};

  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }

  err = EnterAtMode();

  if (err != kLoraModuleError::kNoError) {
    AE_TELED_ERROR("Can't enter to the AT command mode.");
    return false;
  }

  at_comm_support_->SendATCommand("AT+RESET");  // Reset module
  if (err = CheckResponse("OK", 1000, "AT command error!");
      err != kLoraModuleError::kNoError) {
    AE_TELED_ERROR("AT command error {}", err);
    return false;
  }

  err = ExitAtMode();

  if (err != kLoraModuleError::kNoError) {
    AE_TELED_ERROR("Can't exit from the AT command mode.");
    return false;
  }

  return true;
};

ConnectionLoraIndex DxSmartLr02LoraModule::OpenNetwork(ae::Protocol protocol,
                                                       std::string const& host,
                                                       std::uint16_t port) {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port not open");
    return -1;
  }

  auto connect_index = static_cast<ConnectionLoraIndex>(connect_vec_.size());
  connect_vec_.emplace_back(
      LoraConnection{connect_index, protocol, host, port});

  return connect_index;
};

void DxSmartLr02LoraModule::CloseNetwork(
    ae::ConnectionLoraIndex connect_index) {
  if (connect_index >= connect_vec_.size()) {
    AE_TELED_ERROR("Connection index overflow");
    return;
  }
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port not open");
    return;
  }
  /* Removing connection
    auto const& connection =
      connect_vec_.at(static_cast<std::size_t>(connect_index));*/

  connect_vec_.erase(connect_vec_.begin() + connect_index);
};

void DxSmartLr02LoraModule::WritePacket(ae::ConnectionLoraIndex connect_index,
                                        ae::DataBuffer const& data) {
  LoraPacket lora_packet{};

  auto const& connection =
      connect_vec_.at(static_cast<std::size_t>(connect_index));

  lora_packet.connection = connection;
  lora_packet.length = data.size();
  lora_packet.data = data;
  lora_packet.crc = 0;  // Not implemented yet

  auto packet_data = std::vector<std::uint8_t>{};
  VectorWriter<PacketSize> vw{packet_data};
  auto os = omstream{vw};
  // copy data with size
  os << lora_packet;

  serial_->Write(packet_data);
};

DataBuffer DxSmartLr02LoraModule::ReadPacket(
    ae::ConnectionLoraIndex /* connect_index*/, ae::Duration /* timeout*/) {
  LoraPacket lora_packet{};
  DataBuffer data{};

  auto response = serial_->Read();
  std::vector<std::uint8_t> packet_data(response->begin(), response->end());
  VectorReader<PacketSize> vr(packet_data);
  auto is = imstream{vr};
  // copy data with size
  is >> lora_packet;

  data = lora_packet.data;

  return data;
};

bool DxSmartLr02LoraModule::SetPowerSaveParam(std::string const& /* psp */) {
  return true;
};

bool DxSmartLr02LoraModule::PowerOff() { return true; };

bool DxSmartLr02LoraModule::SetLoraModuleAddress(std::uint16_t const& address) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  at_comm_support_->SendATCommand(
      "AT+MAC" + AdressToString(address));  // Set module address

  err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    return false;
  }

  return true;
};

bool DxSmartLr02LoraModule::SetLoraModuleChannel(std::uint8_t const& channel) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  if (channel > 0x1E) {
    return false;
  }

  at_comm_support_->SendATCommand(
      "AT+CHANNEL" + ChannelToString(channel));  // Set module channel

  err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    return false;
  }

  return true;
};

bool DxSmartLr02LoraModule::SetLoraModuleMode(kLoraModuleMode const& mode) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  at_comm_support_->SendATCommand(
      "AT+MODE" + std::to_string(static_cast<int>(mode)));  // Set module mode

  err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    return false;
  }

  return true;
};

bool DxSmartLr02LoraModule::SetLoraModuleLevel(kLoraModuleLevel const& level) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  at_comm_support_->SendATCommand(
      "AT+LEVEL" +
      std::to_string(static_cast<int>(level)));  // Set module level

  err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    return false;
  }

  return true;
};

bool DxSmartLr02LoraModule::SetLoraModulePower(kLoraModulePower const& power) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  at_comm_support_->SendATCommand(
      "AT+POWE" + std::to_string(static_cast<int>(power)));  // Set module power

  err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    return false;
  }

  return true;
};

bool DxSmartLr02LoraModule::SetLoraModuleBandWidth(
    kLoraModuleBandWidth const& band_width) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  at_comm_support_->SendATCommand(
      "AT+BW" +
      std::to_string(static_cast<int>(band_width)));  // Set module bandwidth

  err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    return false;
  }

  return true;
};

bool DxSmartLr02LoraModule::SetLoraModuleCodingRate(
    kLoraModuleCodingRate const& coding_rate) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  at_comm_support_->SendATCommand(
      "AT+CR" +
      std::to_string(static_cast<int>(coding_rate)));  // Set module coding rate

  err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    return false;
  }

  return true;
};

bool DxSmartLr02LoraModule::SetLoraModuleSpreadingFactor(
    kLoraModuleSpreadingFactor const& spreading_factor) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  at_comm_support_->SendATCommand(
      "AT+SF" + std::to_string(static_cast<int>(
                    spreading_factor)));  // Set module spreading factor

  err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    return false;
  }

  return true;
};

bool DxSmartLr02LoraModule::SetLoraModuleCRCCheck(
    kLoraModuleCRCCheck const& crc_check) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  at_comm_support_->SendATCommand(
      "AT+CRC" +
      std::to_string(static_cast<int>(crc_check)));  // Set module crc check

  err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    return false;
  }

  return true;
};

bool DxSmartLr02LoraModule::SetLoraModuleIQSignalInversion(
    kLoraModuleIQSignalInversion const& signal_inversion) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  at_comm_support_->SendATCommand(
      "AT+IQ" + std::to_string(static_cast<int>(
                    signal_inversion)));  // Set module signal inversion

  err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    return false;
  }

  return true;
};

// =============================private members=========================== //
kLoraModuleError DxSmartLr02LoraModule::CheckResponse(
    std::string const& response, std::uint32_t const wait_time,
    std::string const& error_message) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  if (!at_comm_support_->WaitForResponse(
          response, std::chrono::milliseconds(wait_time))) {
    AE_TELED_ERROR(error_message);
    err = kLoraModuleError::kAtCommandError;
  }

  return err;
}

kLoraModuleError DxSmartLr02LoraModule::EnterAtMode() {
  kLoraModuleError err{kLoraModuleError::kNoError};

  if (at_mode_ == false) {
    at_comm_support_->SendATCommand("+++");
    if (err = CheckResponse("Entry AT", 1000, "EnterAtMode command error!");
        err != kLoraModuleError::kNoError) {
      AE_TELED_ERROR("AT command error {}", err);
    } else {
      at_mode_ = true;
    }
  }

  return err;
};

kLoraModuleError DxSmartLr02LoraModule::ExitAtMode() {
  kLoraModuleError err{kLoraModuleError::kNoError};
  uint8_t answer_cnt{0};

  if (at_mode_ == true) {
    at_comm_support_->SendATCommand("+++");
    if (err = CheckResponse("Exit AT", 1000, "ExitAtMode command error!");
        err != kLoraModuleError::kNoError) {
      AE_TELED_ERROR("AT command error {}", err);
    } else {
      answer_cnt++;
    }
  }

  if (answer_cnt == 1) {
    if (err = CheckResponse("Power on", 1000, "ExitAtMode command error!");
        err != kLoraModuleError::kNoError) {
      AE_TELED_ERROR("AT command error {}", err);
    } else {
      answer_cnt++;
    }
  }

  if (answer_cnt == 2) {
    at_mode_ = false;
  }

  return err;
};

kLoraModuleError DxSmartLr02LoraModule::SetupSerialPort(
    SerialInit& serial_init) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  err = SetBaudRate(serial_init.baud_rate);

  if (err != kLoraModuleError::kNoError) {
    return err;
  }

  err = SetParity(serial_init.parity);

  if (err != kLoraModuleError::kNoError) {
    return err;
  }

  err = SetStopBits(serial_init.stop_bits);

  return err;
}

kLoraModuleError DxSmartLr02LoraModule::SetBaudRate(kBaudRate baud_rate) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  auto it = baud_rate_commands_lr02.find(baud_rate);
  if (it == baud_rate_commands_lr02.end()) {
    err = kLoraModuleError::kBaudRateError;
    return err;
  }

  at_comm_support_->SendATCommand(it->second);
  err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    err = kLoraModuleError::kBaudRateError;
  }

  return err;
}

kLoraModuleError DxSmartLr02LoraModule::SetParity(kParity parity) {
  switch (parity) {
    case kParity::kNoParity:
      at_comm_support_->SendATCommand("AT+PARI0");  // Set no parity
      break;
    case kParity::kOddParity:
      at_comm_support_->SendATCommand("AT+PARI1");  // Set odd parity
      break;
    case kParity::kEvenParity:
      at_comm_support_->SendATCommand("AT+PARI2");  // Set even parity
      break;
    default:
      return kLoraModuleError::kParityError;
      break;
  }

  auto err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    return kLoraModuleError::kParityError;
  }

  return kLoraModuleError::kNoError;
}

kLoraModuleError DxSmartLr02LoraModule::SetStopBits(kStopBits stop_bits) {
  switch (stop_bits) {
    case kStopBits::kOneStopBit:
      at_comm_support_->SendATCommand("AT+STOP0");  // 0 stop bits
      break;
    case kStopBits::kTwoStopBit:
      at_comm_support_->SendATCommand("AT+PARI1");  // 2 stop bits
      break;
    default:
      return kLoraModuleError::kStopBitsError;
      break;
  }

  auto err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    return kLoraModuleError::kStopBitsError;
  }

  return kLoraModuleError::kNoError;
}

kLoraModuleError DxSmartLr02LoraModule::SetupLoraNet(
    LoraModuleInit& lora_module_init) {
  // Module address
  if (!SetLoraModuleAddress(lora_module_init.lora_module_my_adress)) {
    return kLoraModuleError::kLoraAddressError;
  };
  // Module channel
  if (!SetLoraModuleChannel(lora_module_init.lora_module_channel)) {
    return kLoraModuleError::kLoraChannelError;
  };
  // Module mode
  if (!SetLoraModuleMode(lora_module_init.lora_module_mode)) {
    return kLoraModuleError::kLoraModeError;
  };
  // Module level
  if (!SetLoraModuleLevel(lora_module_init.lora_module_level)) {
    return kLoraModuleError::kLoraLevelError;
  };
  // Module power
  if (!SetLoraModulePower(lora_module_init.lora_module_power)) {
    return kLoraModuleError::kLoraPowerError;
  };
  // Module BandWidth
  if (!SetLoraModuleBandWidth(lora_module_init.lora_module_band_width)) {
    return kLoraModuleError::kLoraBandWidthError;
  };
  // Module CodingRate
  if (!SetLoraModuleCodingRate(lora_module_init.lora_module_coding_rate)) {
    return kLoraModuleError::kLoraCodingRateError;
  };
  // Module spreading factor
  if (!SetLoraModuleSpreadingFactor(
          lora_module_init.lora_module_spreading_factor)) {
    return kLoraModuleError::kLoraSFError;
  };
  // Module crc check
  if (!SetLoraModuleCRCCheck(lora_module_init.lora_module_crc_check)) {
    return kLoraModuleError::kLoraCRCError;
  };
  // Module signal inversion
  if (!SetLoraModuleIQSignalInversion(
          lora_module_init.lora_module_signal_inversion)) {
    return kLoraModuleError::kLoraSIError;
  };

  return kLoraModuleError::kNoError;
}

std::string DxSmartLr02LoraModule::AdressToString(uint16_t value) {
  uint8_t high = static_cast<uint8_t>((value >> 8));  // High byte
  uint8_t low = static_cast<uint8_t>(value & 0xFF);   // Low byte
  char buffer[7];                                     // Buffer for string

  // Formatting hex string
  std::snprintf(buffer, sizeof(buffer), "%02x,%02x", high, low);

  return std::string(buffer);
}

std::string DxSmartLr02LoraModule::ChannelToString(uint8_t value) {
  char buffer[5];  // Buffer for string

  // Formatting hex string
  std::snprintf(buffer, sizeof(buffer), "%02x", value);

  return std::string(buffer);
}

}  // namespace ae
