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

#include "aether/modems/sim7070_at_modem.h"
#include "aether/serial_ports/serial_port_factory.h"
#include "aether/adapters/adapter_tele.h"

namespace ae {

Sim7070AtModem::Sim7070AtModem(ModemInit modem_init, Domain* domain) : IModemDriver(modem_init, domain) {
  serial_ = SerialPortFactory::CreatePort(modem_init.serial_init);
};

void Sim7070AtModem::Init() {
  kModemError err{kModemError::kNoError};

  if (serial_->GetConnected()) {
    if (err == kModemError::kNoError) {
      SendATCommand("AT");  // Checking the connection
      err = CheckResponce("OK", 1000, "AT command error!");
    }
    if (err == kModemError::kNoError) {
      SendATCommand("ATE0");  // Turning off the echo
      err = CheckResponce("OK", 1000, "ATE command error!");
    }
    if (err == kModemError::kNoError) {
      SendATCommand("AT+CMEE=2");  // Enabling extended errors
      err = CheckResponce("OK", 1000, "AT+CMEE command error!");
    }
  } else {
    err = kModemError::kSerialPortError;
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Sim7070AtModem::Start() {
  kModemError err{kModemError::kNoError};
  ModemInit modem_init = GetModemInit();

  if (serial_->GetConnected()) {
    // Configuring modem settings
    if (err == kModemError::kNoError) {
      err = SetBaudRate(modem_init.serial_init.baud_rate);
    }

    // Disabling full functionality
    if (err == kModemError::kNoError) {
      SendATCommand("AT+CFUN=0");
      err = CheckResponce("OK", 1000, "AT+CFUN command error!");
    }

    if (err == kModemError::kNoError) {
      err = SetNetMode(kModemMode::kModeLTEOnly);
    }

    if (err == kModemError::kNoError) {
      err = SetNetMode(kModemMode::kModeNbIot);
    }

    // Enabling full functionality
    if (err == kModemError::kNoError) {
      SendATCommand("AT+CFUN=1");
      err = CheckResponce("OK", 1000, "AT+CFUN command error!");
      err = CheckResponce("+CPIN: READY", 10000, "AT+CFUN command error!");
    }

    // Check Sim card
    err = CheckSimStatus();
    if (err == kModemError::kNoError && modem_init.use_pin == true) {
      err = SetupSim(modem_init.pin);
    }

    if (err == kModemError::kNoError) {
      err = SetupNetwork(modem_init.operator_name, modem_init.operator_code,
                         modem_init.apn_name, modem_init.apn_user,
                         modem_init.apn_pass, modem_init.modem_mode,
                         modem_init.auth_type);
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

void Sim7070AtModem::Stop() {
  kModemError err{kModemError::kNoError};

  if (serial_->GetConnected()) {
    if (err == kModemError::kNoError) {
      SendATCommand("ATZ");  // Reset modem settings correctly
      err = CheckResponce("OK", 1000, "ATZ command error!");
    }

    // Disabling full functionality
    if (err == kModemError::kNoError) {
      SendATCommand("AT+CFUN=0");
      err = CheckResponce("OK", 1000, "AT+CFUN command error!");
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

void Sim7070AtModem::OpenNetwork(std::int8_t& connect_index,
                                 ae::Protocol const protocol,
                                 std::string const host,
                                 std::uint16_t const port) {
  auto connection = connect_vec_.back();
  connection.context_index += 1;
  connection.connect_index += 1;
  connect_vec_.push_back(connection);
  connect_index = static_cast<std::uint8_t>(connect_vec_.size() - 1);
  std::string context_i_str = std::to_string(connection.context_index);
  std::string connect_i_str = std::to_string(connection.connect_index);
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

  if (serial_->GetConnected()) {
    if (err == kModemError::kNoError) {
      // AT+CNACT=0,1 // Activate the PDP context
      SendATCommand("AT+CNACT=" + context_i_str + ",1");
      err = CheckResponce("OK", 1000, "AT+CNACT command error!");
    }

    if (err != kModemError::kNoError) {
      // AT+CNACT=0,0 // Deactivate the PDP context
      SendATCommand("AT+CNACT=" + context_i_str + ",0");

      err = CheckResponce("+APP PDP: 0,DEACTIVE", 10000,
                          "AT+CNACT command error!");

      if (err == kModemError::kNoError) {
        // AT+CNACT=0,1 // Activate the PDP context
        SendATCommand("AT+CNACT=" + context_i_str + ",1");
        err = CheckResponce("OK", 1000, "AT+CNACT command error!");
      }
    }

    err = CheckResponce("+APP PDP: 0,ACTIVE", 5000, "AT+CNACT command error!");

    if (err == kModemError::kNoError) {
      // AT+CAOPEN=0,0,"UDP","URL",PORT
      SendATCommand("AT+CAOPEN=" + context_i_str + "," + connect_i_str + ",\"" +
                    protocol_str + "\",\"" + host + "\"," + port_str);
      err = CheckResponce("OK", 1000, "AT+CAOPEN command error!");
    }

    if (err != kModemError::kNoError) {
      // AT+CACLOSE=0 // Close TCP/UDP socket 0.
      SendATCommand("AT+CACLOSE=" + connect_i_str);
      err = CheckResponce("OK", 1000, "AT+CNACT command error!");

      if (err == kModemError::kNoError) {
        // AT+CAOPEN=0,0,"UDP","URL",PORT
        SendATCommand("AT+CAOPEN=" + context_i_str + "," + connect_i_str +
                      ",\"" + protocol_str + "\",\"" + host + "\"," + port_str);
        err = CheckResponce("OK", 1000, "AT+CAOPEN command error!");
      }
    }

  } else {
    err = kModemError::kSerialPortError;
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Sim7070AtModem::CloseNetwork(std::int8_t const connect_index) {
  kModemError err{kModemError::kNoError};

  if (connect_index >= connect_vec_.size()) {
    err = kModemError::kConnectIndex;
  } else if (!serial_->GetConnected()) {
    err = kModemError::kSerialPortError;
  } else {
    auto connection = connect_vec_.back();
    std::string context_i_str = std::to_string(connection.context_index);
    std::string connect_i_str = std::to_string(connection.connect_index);

    if (serial_->GetConnected()) {
      if (err == kModemError::kNoError) {
        // AT+CACLOSE=0 // Close TCP/UDP socket 0.
        SendATCommand("AT+CACLOSE=" + connect_i_str);
        err = CheckResponce("OK", 1000, "AT+CNACT command error!");
        // If error then already closed
        err = kModemError::kNoError;
      }

      if (err == kModemError::kNoError) {
        // AT+CNACT=0,0 // Deactivate the PDP context
        SendATCommand("AT+CNACT=" + context_i_str + ",0");
        err = CheckResponce("OK", 1000, "AT+CNACT command error!");
      }
    }

    if (err != kModemError::kNoError) {
      modem_error_event_.Emit(static_cast<int>(err));
    }
  }
}

void Sim7070AtModem::WritePacket(std::int8_t const connect_index,
                                 std::vector<std::uint8_t> const& data) {
  kModemError err{kModemError::kNoError};

  if (connect_index >= connect_vec_.size()) {
    err = kModemError::kConnectIndex;
  } else if (!serial_->GetConnected()) {
    err = kModemError::kSerialPortError;
  } else {
    auto connection = connect_vec_[connect_index];
    std::string connect_i_str = std::to_string(connection.connect_index);

    if (err == kModemError::kNoError) {
      // AT+CASEND=0,<length>
      // Send TCP/UDP data 0.
      SendATCommand("AT+CASEND=" + connect_i_str + "," +
                    std::to_string(data.size()));
      err = CheckResponce(">", 1000, "AT+CASEND command error!");
    }

    if (err == kModemError::kNoError) {
      serial_->WriteData(data);
      err = CheckResponce("OK", 1000, "AT+CASEND command error!");
    }
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Sim7070AtModem::ReadPacket(std::int8_t const connect_index,
                                std::vector<std::uint8_t>& data,
                                std::int32_t const timeout) {
  std::size_t size{};
  kModemError err{kModemError::kNoError};

  if (connect_index >= connect_vec_.size()) {
    err = kModemError::kConnectIndex;
  } else if (!serial_->GetConnected()) {
    err = kModemError::kSerialPortError;
  } else {
    auto connection = connect_vec_[connect_index];
    std::string connect_i_str = std::to_string(connection.connect_index);

    if (err == kModemError::kNoError) {
      // AT+CAACK=0
      // Query send data information of the TCP/UDP
      // connection with an identifier 0.
      SendATCommand("AT+CAACK=" + connect_i_str);
      // +CAACK: 5,0 // Total size of sent data is 5 and unack data is 0
      auto response = serial_->ReadData();
      std::string response_string(response->begin(), response->end());
      auto start = response_string.find("+CAACK: ") + 8;
      auto stop = response_string.find(",");
      if (stop > start && start != std::string::npos &&
          stop != std::string::npos) {
        size = std::stoi(response_string.substr(start, stop - start));
        AE_TELED_DEBUG("Size {}", size);
      } else {
        size = 0;
      }
    }

    if (size > 0) {
      err = CheckResponce("+CADATAIND", 1000, "+CADATAIND command error!");
      // AT+CARECV=0,<length> Receive data via an established connection
      SendATCommand("AT+CARECV=" + connect_i_str + "," + std::to_string(size));
      auto response = serial_->ReadData();
      std::string response_string(response->begin(), response->end());
      auto start = response_string.find(",") + 1;
      std::vector<std::uint8_t> response_vector(
          response->begin() + start, response->begin() + start + size);
      data = response_vector;
      AE_TELED_DEBUG("Data {}", data);
    }
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }

  AE_TELE_ERROR(kAdapterSerialNotOpen, "Timeout {}", timeout);
}

void Sim7070AtModem::PowerOff() {
  kModemError err{kModemError::kNoError};

  if (serial_->GetConnected()) {
    if (err == kModemError::kNoError) {
      SendATCommand("AT+CPOWD=1");  // Set modem power OFF
      err = CheckResponce("OK", 1000, "Power off error!");
    }
  } else {
    err = kModemError::kSerialPortError;
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

//=============================private members================================//
kModemError Sim7070AtModem::CheckResponce(std::string responce,
                                          std::uint32_t wait_time,
                                          std::string error_message) {
  kModemError err{kModemError::kNoError};

  if (!WaitForResponse(responce, std::chrono::milliseconds(wait_time))) {
    AE_TELE_ERROR(kAdapterModemAtError, error_message);
    err = kModemError::kAtCommandError;
  }

  return err;
}

kModemError Sim7070AtModem::SetBaudRate(kModemBaudRate rate) {
  kModemError err{kModemError::kNoError};
  
  auto it = baud_rate_commands_sim7070.find(rate);
  if (it == baud_rate_commands_sim7070.end()) {
    err = kModemError::kBaudRateError;
    return err;
  } else {
    SendATCommand(it->second);
  }

  err = CheckResponce("OK", 1000, "No response from modem!");
  if (err != kModemError::kNoError) {
    err = kModemError::kBaudRateError;
  }

  return err;
}

kModemError Sim7070AtModem::CheckSimStatus() {
  kModemError err{kModemError::kNoError};

  SendATCommand("AT+CPIN?");  // Check SIM card status
  err = CheckResponce("OK", 1000, "SIM card error!");
  if (err != kModemError::kNoError) {
    err = kModemError::kCheckSimStatus;
  }

  return err;
}

kModemError Sim7070AtModem::SetupSim(const std::uint8_t pin[4]) {
  kModemError err{kModemError::kNoError};

  auto pin_string = PinToString(pin);

  if (pin_string == "ERROR") {
    err = kModemError::kPinWrong;
    return err;
  }

  SendATCommand("AT+CPIN=" + pin_string);  // Check SIM card status
  err = CheckResponce("OK", 1000, "SIM card PIN error!");
  if (err != kModemError::kNoError) {
    err = kModemError::kSetupSim;
  }

  return err;
}

kModemError Sim7070AtModem::SetNetMode(kModemMode modem_mode) {
  kModemError err{kModemError::kNoError};

  switch (modem_mode) {
    case kModemMode::kModeAuto:
      SendATCommand("AT+CNMP=2");  // Set modem mode Auto
      break;
    case kModemMode::kModeGSMOnly:
      SendATCommand("AT+CNMP=13");  // Set modem mode GSMOnly
      break;
    case kModemMode::kModeLTEOnly:
      SendATCommand("AT+CNMP=38");  // Set modem mode LTEOnly
      break;
    case kModemMode::kModeGSMLTE:
      SendATCommand("AT+CNMP=51");  // Set modem mode GSMLTE
      break;
    case kModemMode::kModeCatM:
      SendATCommand("AT+CMNB=1");  // Set modem mode CatM
      break;
    case kModemMode::kModeNbIot:
      SendATCommand("AT+CMNB=2");  // Set modem mode NbIot
      break;
    case kModemMode::kModeCatMNbIot:
      SendATCommand("AT+CMNB=3");  // Set modem mode CatMNbIot
      break;
    default:
      err = kModemError::kSetNetMode;
      return err;
      break;
  }

  err = CheckResponce("OK", 1000, "No response from modem!");
  if (err != kModemError::kNoError) {
    err = kModemError::kSetNetMode;
  }

  return err;
}

kModemError Sim7070AtModem::SetupNetwork(
    std::string operator_name, std::string operator_code, std::string apn_name,
    std::string apn_user, std::string apn_pass, kModemMode modem_mode,
    kAuthType auth_type) {
  kModemError err{kModemError::kNoError};
  std::string mode{"0"}, type{"0"};

  if (modem_mode == kModemMode::kModeAuto ||
      modem_mode == kModemMode::kModeGSMOnly ||
      modem_mode == kModemMode::kModeLTEOnly ||
      modem_mode == kModemMode::kModeGSMLTE ||
      modem_mode == kModemMode::kModeCatMNbIot) {
    mode = "0";
  } else if (modem_mode == kModemMode::kModeCatM) {
    mode = "7";
  } else if (modem_mode == kModemMode::kModeNbIot) {
    mode = "9";
  }

  if (auth_type == kAuthType::kAuthTypeNone) {
    type = "0";
  } else if (auth_type == kAuthType::kAuthTypePAP) {
    type = "1";
  } else if (auth_type == kAuthType::kAuthTypeCHAP) {
    type = "2";
  } else if (auth_type == kAuthType::kAuthTypePAPCHAP) {
    type = "3";
  }

  // Connect to the network
  if (!operator_name.empty()) {
    // Operator long name
    SendATCommand("AT+COPS=1,0,\"" + operator_name + "\"," + mode);
  } else if (!operator_code.empty()) {
    // Operator code
    SendATCommand("AT+COPS=1,2,\"" + operator_code + "\"," + mode);
  } else {
    // Auto
    SendATCommand("AT+COPS=0");
  }

  err = CheckResponce("OK", 120000, "No response from modem!");
  if (err == kModemError::kNoError) {
    SendATCommand("AT+CGDCONT=0,\"IP\",\"" + apn_name + "\"");
    err = CheckResponce("OK", 1000, "No response from modem!");
  }

  if (err == kModemError::kNoError) {
    SendATCommand("AT+CNCFG=0,0,\"" + apn_name + "\",\"" + apn_user + "\",\"" +
                  apn_pass + "\"," + type);
    err = CheckResponce("OK", 1000, "No response from modem!");
  }

  if (err == kModemError::kNoError) {
    SendATCommand("AT+CREG=1;+CGREG=1;+CEREG=1");
    err = CheckResponce("OK", 1000, "No response from modem!");
  }

  if (err == kModemError::kNoError) {
    err = kModemError::kSetNetwork;
  }

  return err;
}

kModemError Sim7070AtModem::SetupProtoPar() {
  kModemError err{kModemError::kNoError};

  // AT+CACFG	Set transparent parameters	OK
  // AT+CASSLCFG	Set SSL parameters	OK

  return err;
}

void Sim7070AtModem::SendATCommand(const std::string& command) {
  std::vector<uint8_t> data(command.begin(), command.end());
  data.push_back('\r');  // Adding a carriage return symbols
  data.push_back('\n');
  serial_->WriteData(data);
}

bool Sim7070AtModem::WaitForResponse(const std::string& expected,
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

std::string Sim7070AtModem::PinToString(const std::uint8_t pin[4]) {
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
