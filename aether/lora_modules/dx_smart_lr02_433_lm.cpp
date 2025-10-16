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

#include <bitset>
#include <string_view>

#include "aether/format/format.h"
#include "aether/misc/from_chars.h"
#include "aether/actions/pipeline.h"
#include "aether/actions/gen_action.h"
//#include "aether/actions/failed_action.h"
#include "aether/serial_ports/serial_port_factory.h"

#include "aether/lora_modules/lora_modules_tele.h"

namespace ae {

DxSmartLr02LoraModule::DxSmartLr02LoraModule(ActionContext action_context,
                                             IPoller::ptr poller,
                                             LoraModuleInit lora_module_init)
    : action_context_{action_context},
      lora_module_init_{std::move(lora_module_init)},
      serial_{SerialPortFactory::CreatePort(action_context_, std::move(poller),
                                            lora_module_init_.serial_init)},
      at_comm_support_{action_context_, *serial_} {
  Init();
  Start();
}

DxSmartLr02LoraModule::~DxSmartLr02LoraModule() { Stop(); }

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::Init() {
  return MakeActionPtr<Pipeline>(
      action_context_,
      // Enter AT command mode
      Stage([this, psp]() {
    return EnterAtMode(); }),
      Stage([this]() {
    return at_comm_support_.SendATCommand("AT"); }),
      Stage([this]() {
    return at_comm_support_.WaitForResponse("OK",
                                            std::chrono::milliseconds{1000});
      }),
      Stage([this, psp]() {
    return SetupSerialPort(lora_module_init.serial_init); }),
      Stage([this, psp]() {
    return SetupLoraNet(lora_module_init); }),
      // Exit AT command mode
      Stage([this, psp]() {
    return ExitAtMode(); })
      ));
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::Start() {
  return MakeActionPtr<Pipeline>(
      action_context_,
      // Enter AT command mode
      Stage([this, psp]() {
    return EnterAtMode(); }),
      // Exit AT command mode
      Stage([this, psp]() {
    return ExitAtMode(); })
      ));
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::Stop() {
  return MakeActionPtr<Pipeline>(
      action_context_,
      // Enter AT command mode
      Stage([this, psp]() {
    return EnterAtMode(); }),
      Stage([this]() {
    return at_comm_support_.SendATCommand("AT+RESET"); }),
      Stage([this]() {
    return at_comm_support_.WaitForResponse("OK",
                                            std::chrono::milliseconds{1000});
      }),
      // Exit AT command mode
      Stage([this, psp]() {
    return ExitAtMode(); })
      ));
};

ActionPtr<DxSmartLr02LoraModule::OpenNetworkOperation>
DxSmartLr02LoraModule::OpenNetwork(ae::Protocol protocol,
                                   std::string const& host,
                                   std::uint16_t port) {
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

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetPowerSaveParam(std::string const& psp) {
    return MakeActionPtr<Pipeline>(
      action_context_,
      // Enter AT command mode
      Stage([this, psp]() {
    return EnterAtMode(); }),
      // Exit AT command mode
      Stage([this, psp]() {
    return ExitAtMode(); }),
      ));
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::PowerOff() {
  return {};
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetLoraModuleAddress(std::uint16_t const& address) {
  return MakeActionPtr<Pipeline>(action_context_, Stage([this]() {
                                   return at_comm_support_.SendATCommand(
                                       "AT+MAC" + AdressToString(address));
                                 }),
                                 Stage([this]() {
                                   return at_comm_support_.WaitForResponse(
                                       "OK", std::chrono::milliseconds{1000});
                                 }));
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetLoraModuleChannel(std::uint8_t const& channel) {
  if (channel > 0x1E) {
    return {};
  }

  return MakeActionPtr<Pipeline>(action_context_, Stage([this]() {
                                   return at_comm_support_.SendATCommand(
                                       "AT+CHANNEL" + ChannelToString(channel));
                                 }),
                                 Stage([this]() {
                                   return at_comm_support_.WaitForResponse(
                                       "OK", std::chrono::milliseconds{1000});
                                 }));
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetLoraModuleMode(kLoraModuleMode const& mode) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this]() {
        return at_comm_support_.SendATCommand(
            "AT+MODE" + std::to_string(static_cast<int>(mode)));
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetLoraModuleLevel(kLoraModuleLevel const& level) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this]() {
        return at_comm_support_.SendATCommand(
            "AT+LEVEL" + std::to_string(static_cast<int>(level)));
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetLoraModulePower(kLoraModulePower const& power) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this]() {
        return at_comm_support_.SendATCommand(
            "AT+POWE" + std::to_string(static_cast<int>(power)));
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetLoraModuleBandWidth(
    kLoraModuleBandWidth const& band_width) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this]() {
        return at_comm_support_.SendATCommand(
            "AT+BW" + std::to_string(static_cast<int>(band_width)));
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetLoraModuleCodingRate(
    kLoraModuleCodingRate const& coding_rate) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this]() {
        return at_comm_support_.SendATCommand(
            "AT+CR" + std::to_string(static_cast<int>(coding_rate)));
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetLoraModuleSpreadingFactor(
    kLoraModuleSpreadingFactor const& spreading_factor) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this]() {
        return at_comm_support_.SendATCommand(
            "AT+SF" + std::to_string(static_cast<int>(spreading_factor)));
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetLoraModuleCRCCheck(
    kLoraModuleCRCCheck const& crc_check) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this]() {
        return at_comm_support_.SendATCommand(
            "AT+CRC" + std::to_string(static_cast<int>(crc_check)));
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetLoraModuleIQSignalInversion(
    kLoraModuleIQSignalInversion const& signal_inversion) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this]() {
        return at_comm_support_.SendATCommand(
            "AT+IQ" + std::to_string(static_cast<int>(signal_inversion)));
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
};

// =============================private members=========================== //
ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::EnterAtMode() {
  if (at_mode_ == false) {
    at_mode_ = true;
    return MakeActionPtr<Pipeline>(
        action_context_,
        Stage([this]() { return at_comm_support_.SendATCommand("+++"); }),
        Stage([this]() {
          return at_comm_support_.WaitForResponse(
              "Entry AT", std::chrono::milliseconds{1000});
        }));
  }

  return {};
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::ExitAtMode() {
  if (at_mode_ == true) {
    at_mode_ = false;
    return MakeActionPtr<Pipeline>(
        action_context_,
        Stage([this]() { return at_comm_support_.SendATCommand("+++"); }),
        Stage([this]() {
          return at_comm_support_.WaitForResponse(
              "Exit AT", std::chrono::milliseconds{1000});
        }),
        Stage([this]() {
          return at_comm_support_.WaitForResponse(
              "Power on", std::chrono::milliseconds{1000});
        }));
  }

  return {};
};

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetupSerialPort(SerialInit& serial_init) {
    return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this, serial_init]() {
    return SetBaudRate(serial_init.baud_rate); }),
      Stage([this, serial_init]() {
    return SetParity(serial_init.parity); }),
      Stage([this, serial_init]() {
    return SetStopBits(serial_init.stop_bits); 
    }));
}

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetBaudRate(kBaudRate baud_rate) {
  auto it = baud_rate_commands_lr02.find(baud_rate);
  if (it == baud_rate_commands_lr02.end()) {
    return {};
  }

  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this]() { return at_comm_support_.SendATCommand(it->second); }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
}

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetParity(kParity parity) {
  std::string cmd{};

  switch (parity) {
    case kParity::kNoParity:
      cmd = "AT+PARI0";  // Set no parity
      break;
    case kParity::kOddParity:
      cmd = "AT+PARI1";  // Set odd parity
      break;
    case kParity::kEvenParity:
      cmd = "AT+PARI2";  // Set even parity
      break;
    default:
      return {};
      break;
  }

  return MakeActionPtr<Pipeline>(action_context_, Stage([this, cmd]() {
                                   return at_comm_support_.SendATCommand(cmd);
                                 }),
                                 Stage([this]() {
                                   return at_comm_support_.WaitForResponse(
                                       "OK", std::chrono::milliseconds{1000});
                                 }));
}

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetStopBits(kStopBits stop_bits) {
  std::string cmd{};

  switch (stop_bits) {
    case kStopBits::kOneStopBit:
      cmd = "AT+STOP0";  // 0 stop bits
      break;
    case kStopBits::kTwoStopBit:
      cmd = "AT+PARI1";  // 2 stop bits
      break;
    default:
      return {};
      break;
  }

  return MakeActionPtr<Pipeline>(action_context_, Stage([this]() {
                                   return at_comm_support_.SendATCommand(cmd);
                                 }),
                                 Stage([this]() {
                                   return at_comm_support_.WaitForResponse(
                                       "OK", std::chrono::milliseconds{1000});
                                 }));
}

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
    DxSmartLr02LoraModule::SetupLoraNet(LoraModuleInit& lora_module_init){
  return MakeActionPtr<Pipeline>(
        action_context_, Stage([this, lora_module_init]() {
          return SetLoraModuleAddress(lora_module_init.lora_module_my_adress);
        }),
        Stage([this, lora_module_init]() {
          return SetLoraModuleChannel(lora_module_init.lora_module_channel);
        }),
        Stage([this, lora_module_init]() {
          return SetLoraModuleMode(lora_module_init.psp.lora_module_mode);
        }),
        Stage([this, lora_module_init]() {
          return SetLoraModuleLevel(lora_module_init.psp.lora_module_level);
        }),
        Stage([this, lora_module_init]() {
          return SetLoraModulePower(lora_module_init.psp.lora_module_power);
        }),
        Stage([this, lora_module_init]() {
          return SetLoraModuleBandWidth(
              lora_module_init.psp.lora_module_band_width);
        }),
        Stage([this, lora_module_init]() {
          return SetLoraModuleCodingRate(
              lora_module_init.psp.lora_module_coding_rate);
        }),
        Stage([this, lora_module_init]() {
          return SetLoraModuleSpreadingFactor(
              lora_module_init.psp.lora_module_spreading_factor);
        }),
        Stage([this, lora_module_init]() {
          return SetLoraModuleCRCCheck(lora_module_init.lora_module_crc_check);
        }),
        Stage([this, lora_module_init]() {
          return SetLoraModuleIQSignalInversion(
              lora_module_init.lora_module_signal_inversion)
        }), ));
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
