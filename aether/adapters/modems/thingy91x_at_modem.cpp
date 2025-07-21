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

#include "aether/adapters/modems/thingy91x_at_modem.h"
#include "aether/adapters/modems/serial_ports/serial_port_factory.h"
#include "aether/adapters/adapter_tele.h"

namespace ae {

Thingy91xAtModem::Thingy91xAtModem(ModemInit modem_init)
    : modem_init_(modem_init) {
  serial_ = SerialPortFactory::CreatePort(modem_init_.serial_init);
};

void Thingy91xAtModem::Init() {
  kModemError err{kModemError::kNoError};

  if (serial_->GetConnected()) {
    if (err == kModemError::kNoError) {
      sendATCommand("AT");  // Checking the connection
      err = CheckResponce("OK", 1000, "AT command error!");
    }
    if (err == kModemError::kNoError) {
      sendATCommand("ATE0");  // Turning off the echo
      err = CheckResponce("OK", 1000, "ATE command error!");
    }
    if (err == kModemError::kNoError) {
      sendATCommand("AT+CMEE=2");  // Enabling extended errors
      err = CheckResponce("OK", 1000, "AT+CMEE command error!");
    }
  } else {
    err = kModemError::kSerialPortError;
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Thingy91xAtModem::Start() {
  kModemError err{kModemError::kNoError};

  if (serial_->GetConnected()) {
    // Configuring modem settings
    /*if (err == kModemError::kNoError) {
      err = SetBaudRate(modem_init_.serial_init.baud_rate);
    }*/

    // Enabling full functionality
    if (err == kModemError::kNoError) {
      sendATCommand("AT+CFUN=1");
      err = CheckResponce("OK", 1000, "AT+CFUN command error!");
    }

    err = CheckSimStatus();
    if (err == kModemError::kNoError && modem_init_.use_pin == true) {
      err = SetupSim(modem_init_.pin);
    }

    if (err == kModemError::kNoError) {
      err = SetNetMode(kModemMode::kModeLTEOnly);
    }
    if (err == kModemError::kNoError) {
      err = SetNetMode(kModemMode::kModeNbIot);
    }

    if (err == kModemError::kNoError) {
      err = SetupNetwork(modem_init_.operator_name, modem_init_.operator_code,
                         modem_init_.apn_name, modem_init_.apn_user,
                         modem_init_.apn_pass, modem_init_.modem_mode,
                         modem_init_.auth_type);
    }
  } else {
    err = kModemError::kSerialPortError;
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  } else {
    modem_connected_event_.Emit(true);
  }
}

void Thingy91xAtModem::Stop() {
  kModemError err{kModemError::kNoError};

  if (serial_->GetConnected()) {
    if (err == kModemError::kNoError) {
      sendATCommand("ATZ");  // Turning off the modem correctly
      err = CheckResponce("OK", 1000, "ATZ command error!");
    }
  } else {
    err = kModemError::kSerialPortError;
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  } else {
    modem_connected_event_.Emit(false);
  }
}

void Thingy91xAtModem::OpenNetwork(std::uint8_t context_index,
                                   std::uint8_t connect_index,
                                   ae::Protocol protocol, std::string host,
                                   std::uint16_t port) {
  std::string context_i_str = std::to_string(context_index);
  std::string connect_i_str = std::to_string(connect_index);
  std::string port_str = std::to_string(port);
  std::string protocol_str;
  kModemError err{kModemError::kNoError};

  switch (protocol) {
    case ae::Protocol::kTcp:
      protocol_str = "TCP";
      break;
    case ae::Protocol::kUdp:
      protocol_str = "UDP";
      break;
    default:
      err = kModemError::kOpenConnection;
      assert(0);
      break;
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Thingy91xAtModem::CloseNetwork(std::uint8_t context_index,
                                    std::uint8_t connect_index) {
  std::string context_i_str = std::to_string(context_index);
  std::string connect_i_str = std::to_string(connect_index);
  kModemError err{kModemError::kNoError};

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Thingy91xAtModem::WritePacket(std::uint8_t connect_index,
                                   std::vector<std::uint8_t> const& data) {
  std::string connect_i_str = std::to_string(connect_index);
  kModemError err{kModemError::kNoError};

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Thingy91xAtModem::ReadPacket(std::uint8_t connect_index,
                                  std::vector<std::uint8_t>& data,
                                  std::size_t& size) {
  std::string connect_i_str = std::to_string(connect_index);
  kModemError err{kModemError::kNoError};

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

//=============================private members================================//
kModemError Thingy91xAtModem::CheckResponce(std::string responce,
                                            std::uint32_t wait_time,
                                            std::string error_message) {
  kModemError err{kModemError::kNoError};

  if (!waitForResponse(responce, std::chrono::milliseconds(wait_time))) {
    AE_TELE_ERROR(kAdapterModemAtError, error_message);
    err = kModemError::kAtCommandError;
  }

  return err;
}

kModemError Thingy91xAtModem::CheckSimStatus() {
  kModemError err{kModemError::kNoError};

  sendATCommand("AT+CPIN?");  // Check SIM card status
  err = CheckResponce("OK", 1000, "SIM card error!");
  if (err != kModemError::kNoError) {
    err = kModemError::kCheckSimStatus;
  }

  return err;
}

kModemError Thingy91xAtModem::SetupSim(const std::uint8_t pin[4]) {
  kModemError err{kModemError::kNoError};

  auto pin_string = pinToString(pin);

  if (pin_string == "ERROR") {
    err = kModemError::kPinWrong;
    return err;
  }

  sendATCommand("AT+CPIN=" + pin_string);  // Check SIM card status
  err = CheckResponce("OK", 1000, "SIM card PIN error!");
  if (err != kModemError::kNoError) {
    err = kModemError::kSetupSim;
  }

  return err;
}

kModemError Thingy91xAtModem::SetNetMode(kModemMode modem_mode) {
  kModemError err{kModemError::kNoError};

  switch (modem_mode) {
    case kModemMode::kModeAuto:
      sendATCommand("AT%XSYSTEMMODE=1,1,0,4");  // Set modem mode Auto
      break;
    case kModemMode::kModeCatM:
      sendATCommand("AT%XSYSTEMMODE=1,0,0,1");  // Set modem mode CatM
      break;
    case kModemMode::kModeNbIot:
      sendATCommand("AT%XSYSTEMMODE=0,1,0,2");  // Set modem mode NbIot
      break;
    case kModemMode::kModeCatMNbIot:
      sendATCommand("AT%XSYSTEMMODE=1,1,0,0");  // Set modem mode CatMNbIot
      break;
    default:
      err = kModemError::kSetNetMode;
      return err;
      break;
  }

  return err;
}

kModemError Thingy91xAtModem::SetupNetwork(
    std::string operator_name, std::string operator_code, std::string apn_name,
    std::string apn_user, std::string apn_pass, kModemMode modem_mode,
    kAuthType auth_type) {
  kModemError err{kModemError::kNoError};

  // Connect to the network
  if (!operator_name.empty()) {
    // Operator long name
    sendATCommand("AT+COPS=1,0,\"" + operator_name);
  } else if (!operator_code.empty()) {
    // Operator code
    sendATCommand("AT+COPS=1,2,\"" + operator_code);
  } else {
    // Auto
    sendATCommand("AT+COPS=0");
  }

  err = CheckResponce("OK", 120000, "No response from modem!");
  if (err == kModemError::kNoError) {
    sendATCommand("AT+CGDCONT=0,\"IP\",\"" + apn_name + "\"");
    err = CheckResponce("OK", 1000, "No response from modem!");
  }

  if (err == kModemError::kNoError) {
    sendATCommand("AT+CEREG=");
    err = CheckResponce("OK", 1000, "No response from modem!");
  }

  AE_TELED_DEBUG("apn_user", apn_user);
  AE_TELED_DEBUG("apn_pass", apn_pass);
  AE_TELED_DEBUG("modem_mode", modem_mode);
  AE_TELED_DEBUG("auth_type", auth_type);
  return err;
}

void Thingy91xAtModem::sendATCommand(const std::string& command) {
  std::vector<uint8_t> data(command.begin(), command.end());
  data.push_back('\r');  // Adding a carriage return symbols
  data.push_back('\n');
  serial_->WriteData(data);
}

bool Thingy91xAtModem::waitForResponse(const std::string& expected,
                                       std::chrono::milliseconds timeout_ms) {
  // Simplified implementation of waiting for a response
  auto start = std::chrono::high_resolution_clock::now();

  while (true) {
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    if (elapsed > timeout_ms) {
      return false;
    }

    if (auto response = serial_->ReadData()) {
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

} /* namespace ae */
