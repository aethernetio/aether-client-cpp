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

#include "aether/modems/bg95_at_modem.h"
#include "aether/serial_ports/serial_port_factory.h"
#include "aether/adapters/adapter_tele.h"

namespace ae {

Bg95AtModem::Bg95AtModem(ModemInit modem_init, Domain* domain)
    : IModemDriver(modem_init, domain) {
  serial_ = SerialPortFactory::CreatePort(modem_init.serial_init);
}

void Bg95AtModem::Init() {
  kModemError err{kModemError::kNoError};

  if (err == kModemError::kNoError) {
    sendATCommand("AT");  // Checking the connection
    err = CheckResponse("OK", 1000, "AT command error!");
  }
  if (err == kModemError::kNoError) {
    sendATCommand("ATE0");  // Turning off the echo
    err = CheckResponse("OK", 1000, "ATE command error!");
  }
  if (err == kModemError::kNoError) {
    sendATCommand("AT+CMEE=2");  // Enabling extended errors
    err = CheckResponse("OK", 1000, "AT+CMEE command error!");
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Bg95AtModem::Start() {
  kModemError err{kModemError::kNoError};
  ModemInit modem_init = GetModemInit();

  // Configuring modem settings
  // Serial port speed
  if (err == kModemError::kNoError) {
    err = SetBaudRate(modem_init.serial_init.baud_rate);
  }

  // Enabling full functionality
  if (err == kModemError::kNoError) {
    sendATCommand("AT+CFUN=1");
    err = CheckResponse("OK", 1000, "AT+CFUN command error!");
  }

  if (err == kModemError::kNoError) {
    err = CheckSimStatus();
  }

  if (err == kModemError::kNoError && modem_init.use_pin == true) {
    err = SetupSim(modem_init.pin);
  }

  if (err == kModemError::kNoError) {
    err = SetupNetwork(modem_init.operator_name, modem_init.operator_code,
                       modem_init.apn_name, modem_init.apn_user,
                       modem_init.apn_pass);
  }

  // float power = 23.5;  // DBm
  // err = SetTxPower(kModemBand::kWCDMA_B1, power);
  // err = GetTxPower(kModemBand::kWCDMA_B1, power);

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  } else {
    modem_connected_event_.Emit(true);
  }
}

void Bg95AtModem::Stop() {}

void Bg95AtModem::OpenNetwork(std::int8_t& connect_index,
                              ae::Protocol const protocol,
                              std::string const host,
                              std::uint16_t const port) {
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Connect index {}", connect_index);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Protocol {}", protocol);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Host {}", host);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Port {}", port);
}

void Bg95AtModem::CloseNetwork(std::int8_t const connect_index) {
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Connect index {}", connect_index);
}

void Bg95AtModem::WritePacket(std::int8_t const connect_index,
                              std::vector<uint8_t> const& data) {
  serial_->Write(data);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Connect index {}", connect_index);
}

void Bg95AtModem::ReadPacket(std::int8_t const connect_index,
                             std::vector<std::uint8_t>& data,
                             std::int32_t timeout) {
  // std::size_t size{};

  auto response = serial_->Read();
  std::vector<std::uint8_t> response_vector(response->begin(), response->end());
  data = response_vector;
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Connect index {}", connect_index);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Timeout {}", timeout);
}

// ============================private members============================= //
kModemError Bg95AtModem::CheckResponse(std::string responce,
                                       std::uint32_t wait_time,
                                       std::string error_message) {
  kModemError err{kModemError::kNoError};

  if (!waitForResponse(responce, std::chrono::milliseconds(wait_time))) {
    AE_TELE_ERROR(kAdapterModemAtError, error_message);
    err = kModemError::kAtCommandError;
  }

  return err;
}

kModemError Bg95AtModem::SetBaudRate(kBaudRate rate) {
  kModemError err{kModemError::kNoError};

  auto it = baud_rate_commands_bg95.find(rate);
  if (it == baud_rate_commands_bg95.end()) {
    err = kModemError::kBaudRateError;
    return err;
  } else {
    sendATCommand(it->second);
  }

  err = CheckResponse("OK", 1000, "No response from modem!");
  if (err != kModemError::kNoError) {
    err = kModemError::kBaudRateError;
  }

  return err;
}

kModemError Bg95AtModem::CheckSimStatus() {
  kModemError err{kModemError::kNoError};

  sendATCommand("AT+CPIN?");  // Check SIM card status
  err = CheckResponse("OK", 1000, "SIM card error!");
  if (err != kModemError::kNoError) {
    err = kModemError::kCheckSimStatus;
  }

  return err;
}

kModemError Bg95AtModem::SetupSim(const std::uint8_t pin[4]) {
  kModemError err{kModemError::kNoError};

  auto pin_string = pinToString(pin);

  if (pin_string == "ERROR") {
    err = kModemError::kPinWrong;
    return err;
  }

  sendATCommand("AT+CPIN=" + pin_string);  // Check SIM card status
  err = CheckResponse("OK", 1000, "SIM card PIN error!");
  if (err != kModemError::kNoError) {
    err = kModemError::kSetupSim;
  }

  return err;
}

kModemError Bg95AtModem::SetTxPower(kModemBand band, const float& power) {
  kModemError err{kModemError::kNoError};
  std::string power_command, hex;

  err = DbmaToHex(band, power, hex);

  if (err == kModemError::kNoError) {
    auto it = set_band_power_bg95.find(band);
    if (it == set_band_power_bg95.end()) {
      err = kModemError::kSetTxPowerBand;
      return err;
    } else {
      if (band == kModemBand::kGSM_850 || band == kModemBand::kGSM_900) {
        power_command = Format(it->second, hex, hex);
      } else {
        power_command = Format(it->second, hex);
      }
      sendATCommand(power_command);
      err = CheckResponse("OK", 1000, "No response from modem!");
    }
  }

  if (err != kModemError::kNoError) {
    err = kModemError::kSetTxPowerBand;
  }

  return err;
}

kModemError Bg95AtModem::GetTxPower(kModemBand band, float& power) {
  kModemError err{kModemError::kNoError};
  std::string power_command, hex;

  if (err == kModemError::kNoError) {
    auto it = get_band_power_bg95.find(band);
    if (it == get_band_power_bg95.end()) {
      err = kModemError::kSetTxPowerBand;
      return err;
    } else {
      power_command = Format(it->second);

      sendATCommand(power_command);
      err = CheckResponse("OK", 1000, "No response from modem!");
    }
  }

  auto response = serial_->Read();

  std::string response_str(response->begin(), response->end());

  hex = response_str;

  HexToDbma(band, power, hex);

  return err;
}

kModemError Bg95AtModem::DbmaToHex(kModemBand band, const float& power,
                                   std::string& hex) {
  kModemError err{kModemError::kNoError};
  std::uint8_t byte1, byte2;
  std::uint16_t word;
  std::stringstream ss;

  if (kModemBand::kWCDMA_B1 <= band && band <= kModemBand::kWCDMA_B8) {
    // kWCDMA bands
    // 22 dBm = (22-6.7)*10= 0x99
    if (power >= 21 && power <= 24.5) {
      byte1 = static_cast<std::uint8_t>((power - 6.7f) * 10);
      ss << std::hex << std::setw(2) << static_cast<int>(byte1)
         << static_cast<int>(byte1);
      hex = ss.str();
    } else {
      err = kModemError::kDbmaToHexRange;
    }
  } else if (kModemBand::kLTE_B1 <= band && band <= kModemBand::kLTE_B41) {
    // kLTE bands
    // 24 dBm = 24*10 = 240 ----> F0
    if (power >= 21 && power <= 24.5) {
      byte1 = static_cast<std::uint8_t>(power * 10);
      ss << std::hex << std::setw(2) << static_cast<int>(byte1);
      hex = ss.str();
    } else {
      err = kModemError::kDbmaToHexRange;
    }
  } else if (kModemBand::kTDS_B34 <= band && band <= kModemBand::kTDS_B39) {
    // kTDS bands
    // 24 dBm = 24*10 = 240 ----> F0
    if (power >= 21 && power <= 24.5) {
      byte1 = static_cast<std::uint8_t>(power * 10);
      ss << std::hex << std::setw(2) << static_cast<int>(byte1);
      hex = ss.str();
    } else {
      err = kModemError::kDbmaToHexRange;
    }
  } else if (kModemBand::kGSM_850 <= band && band <= kModemBand::kGSM_1900) {
    // kGSM bands
    // 32 dBm = 32*100= 3200 = 0x0C80(HEX) = 800C
    if (power >= 26 && power <= 33) {
      word = static_cast<std::uint16_t>(power * 100);
      byte1 = static_cast<std::uint8_t>((word >> 8) & 0xFF);
      byte2 = static_cast<std::uint8_t>((word >> 0) & 0xFF);
      ss << std::hex << std::setw(2) << static_cast<int>(byte1)
         << static_cast<int>(byte2);
      hex = ss.str();
    } else {
      err = kModemError::kDbmaToHexRange;
    }
  } else if (kModemBand::kTDSCDMA_B34 <= band &&
             band <= kModemBand::kTDSCDMA_B39) {
    // kTDSCDMA bands
    // 22 dBm = 22*10+700 = 920 = 0x0398(HEX) = 9803
    if (power >= 21 && power <= 24.5) {
      word = static_cast<std::uint16_t>(power * 10 + 700);
      byte1 = static_cast<std::uint8_t>((word >> 8) & 0xFF);
      byte2 = static_cast<std::uint8_t>((word >> 0) & 0xFF);
      ss << std::hex << std::setw(2) << static_cast<int>(byte2)
         << static_cast<int>(byte1);
      hex = ss.str();
    } else {
      err = kModemError::kDbmaToHexRange;
    }
  } else {
    err = kModemError::kDbmaToHexBand;
  }

  return err;
}

kModemError Bg95AtModem::HexToDbma(kModemBand band, float& power,
                                   const std::string& hex) {
  kModemError err{kModemError::kNoError};

  AE_TELE_ERROR(kAdapterSerialNotOpen, "Band {}", band);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Power {}", power);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Hex {}", hex);

  return err;
}

kModemError Bg95AtModem::SetupNetwork(std::string operator_name,
                                      std::string operator_code,
                                      std::string apn_name,
                                      std::string apn_user,
                                      std::string apn_pass) {
  kModemError err{kModemError::kNoError};

  sendATCommand("AT+COPS=1,2,\"" + operator_code + "\",0");
  // err = CheckResponse("OK", 1000, "No response from modem!");
  // if (err != kModemError::kNoError) {}

  AE_TELE_ERROR(kAdapterSerialNotOpen, "Operator name {}", operator_name);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Operator code {}", operator_code);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "APN name {}", apn_name);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "APN user {}", apn_user);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "APN pass {}", apn_pass);

  return err;
}

void Bg95AtModem::SendATCommand(const std::string& command) {
  std::vector<uint8_t> data(command.begin(), command.end());
  data.push_back('\r');  // Adding a carriage return symbols
  data.push_back('\n');
  serial_->Write(data);
}

bool Bg95AtModem::WaitForResponse(const std::string& expected,
                                  std::chrono::milliseconds timeout_ms) {
  // Simplified implementation of waiting for a response
  auto start = std::chrono::high_resolution_clock::now();

  while (true) {
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    if (elapsed > timeout_ms) {
      return false;
    }

    if (auto response = serial_->Read()) {
      std::string response_str(response->begin(), response->end());
      if (response_str.find(expected) != std::string::npos) {
        return true;
      }
      if (response_str.find("ERROR") != std::string::npos) {
        return false;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

std::string Bg95AtModem::PinToString(const std::uint8_t pin[4]) {
  std::string result{};

  for (int i = 0; i < 4; ++i) {
    if (pin[i] > 9) {
      result = "ERROR";
      break;
    }
    result += static_cast<char>('0' + pin[i]);
  }

  return result;
}

} /* namespace ae */
