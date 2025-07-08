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

#include "aether/adapters/modems/sim7070_at_modem.h"
#include "aether/adapters/modems/serial_ports/serial_port_factory.h"
#include "aether/adapters/adapter_tele.h"

namespace ae {

Sim7070AtModem::Sim7070AtModem(ModemInit modem_init) : modem_init_(modem_init) {
  serial_ = SerialPortFactory::CreatePort(modem_init_.serial_init);
};

void Sim7070AtModem::Init() {
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

void Sim7070AtModem::Setup() {
  kModemError err{kModemError::kNoError};

  if (serial_->GetConnected()) {
    // Configuring modem settings
    if (err == kModemError::kNoError) {
      err = SetBaudRate(modem_init_.serial_init.baud_rate);
    }

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

void Sim7070AtModem::Stop() {
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

void Sim7070AtModem::OpenNetwork(std::uint8_t context_index,
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
      protocol_str = "UDP";  //"TCP";
      break;
    /*case ae::Protocol::kUdp:
      protocol_str = "UDP";
      break;*/
    default:
      err = kModemError::kOpenConnection;
      assert(0);
      break;
  }

  if (serial_->GetConnected()) {
    if (err == kModemError::kNoError) {
      // AT+CNACT=0,1 // Activate the PDP context
      sendATCommand("AT+CNACT=" + context_i_str + ",1");
      err = CheckResponce("OK", 1000, "AT+CNACT command error!");
    }

    if (err != kModemError::kNoError) {
      // AT+CNACT=0,0 // Deactivate the PDP context
      sendATCommand("AT+CNACT=" + context_i_str + ",0");

      err = CheckResponce("+APP PDP: 0,DEACTIVE", 10000,
                          "AT+CNACT command error!");

      if (err == kModemError::kNoError) {
        // AT+CNACT=0,1 // Activate the PDP context
        sendATCommand("AT+CNACT=" + context_i_str + ",1");
        err = CheckResponce("OK", 1000, "AT+CNACT command error!");
      }
    }

    err = CheckResponce("+APP PDP: 0,ACTIVE", 5000, "AT+CNACT command error!");

    if (err == kModemError::kNoError) {
      // AT+CACLOSE=0 // Close TCP/UDP socket 0.
      sendATCommand("AT+CACLOSE=" + connect_i_str);
      err = CheckResponce("OK", 1000, "AT+CNACT command error!");
    }

    if (err == kModemError::kNoError) {
      // AT+CAOPEN=0,0,"UDP","URL",PORT
      sendATCommand("AT+CAOPEN=" + context_i_str + "," + connect_i_str + ",\"" +
                    protocol_str + "\",\"" + host + "\"," + port_str);
      err = CheckResponce("OK", 1000, "AT+CAOPEN command error!");
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

void Sim7070AtModem::CloseNetwork(std::uint8_t context_index,
                                  std::uint8_t connect_index) {
  std::string context_i_str = std::to_string(context_index);
  std::string connect_i_str = std::to_string(connect_index);
  kModemError err{kModemError::kNoError};

  if (err == kModemError::kNoError) {
    // AT+CACLOSE=0 // Close TCP/UDP socket 0.
    sendATCommand("AT+CACLOSE=" + connect_i_str);
    err = CheckResponce("OK", 1000, "AT+CNACT command error!");
  }

  if (err == kModemError::kNoError) {
    // AT+CNACT=0,0 // Deactivate the PDP context
    sendATCommand("AT+CNACT=" + context_i_str + ",0");
    err = CheckResponce("OK", 1000, "AT+CNACT command error!");
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  } else {
    modem_connected_event_.Emit(false);
  }
}

void Sim7070AtModem::WritePacket(std::uint8_t connect_index,
                                 std::vector<uint8_t> const& data) {
  std::string connect_i_str = std::to_string(connect_index);
  kModemError err{kModemError::kNoError};

  if (err == kModemError::kNoError) {
    // AT+CASEND=0,<length> // Send TCP/UDP data 0.
    sendATCommand("AT+CASEND=" + connect_i_str + "," +
                  std::to_string(data.size()));
    err = CheckResponce(">", 1000, "AT+CASEND command error!");
  }

  if (err == kModemError::kNoError) {
    // std::this_thread::sleep_for(std::chrono::milliseconds(250));
    serial_->WriteData(data);
  }
};

void Sim7070AtModem::ReadPacket(std::uint8_t connect_index,
                                std::vector<std::uint8_t>& data) {
  auto response = serial_->ReadData();
  std::vector<std::uint8_t> response_vector(response->begin(), response->end());
  data = response_vector;

  AE_TELE_ERROR(kAdapterSerialNotOpen, "Connect index {}", connect_index);
};

void Sim7070AtModem::PowerOff() {
  kModemError err{kModemError::kNoError};

  if (err == kModemError::kNoError) {
    sendATCommand("AT+CPOWD=1");  // Set modem power OFF
    err = CheckResponce("OK", 1000, "Power off error!");
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  } else {
    modem_connected_event_.Emit(false);
  }
};

//=============================private members================================//
kModemError Sim7070AtModem::CheckResponce(std::string responce,
                                          std::uint32_t wait_time,
                                          std::string error_message) {
  kModemError err{kModemError::kNoError};

  if (!waitForResponse(responce, std::chrono::milliseconds(wait_time))) {
    AE_TELE_ERROR(kAdapterModemAtError, error_message);
    err = kModemError::kAtCommandError;
  }

  return err;
}

kModemError Sim7070AtModem::SetBaudRate(std::uint32_t rate) {
  kModemError err{kModemError::kNoError};

  switch (rate) {
    case 0:
      sendATCommand("AT+IPR=0");  // Set modem usart speed 0
      break;
    case 300:
      sendATCommand("AT+IPR=300");  // Set modem usart speed 300
      break;
    case 600:
      sendATCommand("AT+IPR=600");  // Set modem usart speed 600
      break;
    case 1200:
      sendATCommand("AT+IPR=1200");  // Set modem usart speed 1200
      break;
    case 2400:
      sendATCommand("AT+IPR=2400");  // Set modem usart speed 2400
      break;
    case 4800:
      sendATCommand("AT+IPR=4800");  // Set modem usart speed 4800
      break;
    case 9600:
      sendATCommand("AT+IPR=9600");  // Set modem usart speed 9600
      break;
    case 19200:
      sendATCommand("AT+IPR=19200");  // Set modem usart speed 19200
      break;
    case 38400:
      sendATCommand("AT+IPR=38400");  // Set modem usart speed 38400
      break;
    case 57600:
      sendATCommand("AT+IPR=57600");  // Set modem usart speed 57600
      break;
    case 115200:
      sendATCommand("AT+IPR=115200");  // Set modem usart speed 115200
      break;
    case 230400:
      sendATCommand("AT+IPR=230400");  // Set modem usart speed 230400
      break;
    case 921600:
      sendATCommand("AT+IPR=921600");  // Set modem usart speed 921600
      break;
    case 2000000:
      sendATCommand("AT+IPR=2000000");  // Set modem usart speed 2000000
      break;
    case 2900000:
      sendATCommand("AT+IPR=2900000");  // Set modem usart speed 2900000
      break;
    case 3000000:
      sendATCommand("AT+IPR=3000000");  // Set modem usart speed 3000000
      break;
    case 3200000:
      sendATCommand("AT+IPR=3200000");  // Set modem usart speed 3200000
      break;
    case 3684000:
      sendATCommand("AT+IPR=3684000");  // Set modem usart speed 3684000
      break;
    case 4000000:
      sendATCommand("AT+IPR=4000000");  // Set modem usart speed 4000000
      break;
    default:
      err = kModemError::kBaudRateError;
      return err;
      break;
  }

  err = CheckResponce("OK", 1000, "No response from modem!");
  if (err != kModemError::kNoError) {
    err = kModemError::kBaudRateError;
  }

  return err;
}

kModemError Sim7070AtModem::CheckSimStatus() {
  kModemError err{kModemError::kNoError};

  sendATCommand("AT+CPIN?");  // Check SIM card status
  err = CheckResponce("OK", 1000, "SIM card error!");
  if (err != kModemError::kNoError) {
    err = kModemError::kCheckSimStatus;
  }

  return err;
}

kModemError Sim7070AtModem::SetupSim(const std::uint8_t pin[4]) {
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

kModemError Sim7070AtModem::SetNetMode(kModemMode modem_mode) {
  kModemError err{kModemError::kNoError};

  switch (modem_mode) {
    case kModemMode::kModeAuto:
      sendATCommand("AT+CNMP=2");  // Set modem mode Auto
      break;
    case kModemMode::kModeGSMOnly:
      sendATCommand("AT+CNMP=13");  // Set modem mode GSMOnly
      break;
    case kModemMode::kModeLTEOnly:
      sendATCommand("AT+CNMP=38");  // Set modem mode LTEOnly
      break;
    case kModemMode::kModeGSMLTE:
      sendATCommand("AT+CNMP=51");  // Set modem mode GSMLTE
      break;
    case kModemMode::kModeCatM:
      sendATCommand("AT+CMNB=1");  // Set modem mode CatM
      break;
    case kModemMode::kModeNbIot:
      sendATCommand("AT+CMNB=2");  // Set modem mode NbIot
      break;
    case kModemMode::kModeCatMNbIot:
      sendATCommand("AT+CMNB=3");  // Set modem mode CatMNbIot
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
    sendATCommand("AT+COPS=1,0,\"" + operator_name + "\"," + mode);
  } else if (!operator_code.empty()) {
    // Operator code
    sendATCommand("AT+COPS=1,2,\"" + operator_code + "\"," + mode);
  } else {
    // Auto
    sendATCommand("AT+COPS=0");
  }

  err = CheckResponce("OK", 120000, "No response from modem!");
  if (err == kModemError::kNoError) {
    sendATCommand("AT+CGDCONT=1,\"IP\",\"" + apn_name + "\"");
    err = CheckResponce("OK", 1000, "No response from modem!");
  }

  if (err == kModemError::kNoError) {
    sendATCommand("AT+CNCFG=0,0,\"" + apn_name + "\",\"" + apn_user + "\",\"" +
                  apn_pass + "\"," + type);
    err = CheckResponce("OK", 1000, "No response from modem!");
  }

  if (err == kModemError::kNoError) {
    sendATCommand("AT+CREG=1;+CGREG=1;+CEREG=1");
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

void Sim7070AtModem::sendATCommand(const std::string& command) {
  std::vector<uint8_t> data(command.begin(), command.end());
  data.push_back('\r');  // Adding a carriage return symbols
  data.push_back('\n');
  serial_->WriteData(data);
}

bool Sim7070AtModem::waitForResponse(const std::string& expected,
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
