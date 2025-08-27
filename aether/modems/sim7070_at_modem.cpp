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

Sim7070AtModem::Sim7070AtModem(ModemInit modem_init, Domain* domain)
    : IModemDriver{std::move(modem_init), domain} {
  serial_ = SerialPortFactory::CreatePort(get_modem_init().serial_init);
};

bool Sim7070AtModem::Init() {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }

  SendATCommand("AT");  // Checking the connection
  if (auto err = CheckResponse("OK", 1000, "AT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT command error {}", err);
    return false;
  }

  SendATCommand("ATE0");  // Checking the connection
  if (auto err = CheckResponse("OK", 1000, "ATE0 command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("ATE0 command error {}", err);
    return false;
  }

  SendATCommand("AT+CMEE=2");  // Enabling extended errors
  if (auto err = CheckResponse("OK", 1000, "AT+CMEE command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CMEE command error {}", err);
    return false;
  }

  return true;
}

bool Sim7070AtModem::Start() {
  ModemInit modem_init = get_modem_init();
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }

  if (auto err = SetBaudRate(modem_init.serial_init.baud_rate);
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Set baud rate error {}", err);
    return false;
  }

  // Disabling full functionality
  SendATCommand("AT+CFUN=0");
  if (auto err = CheckResponse("OK", 1000, "AT+CFUN command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CFUN command error {}", err);
    return false;
  }

  if (auto err = SetNetMode(modem_init.modem_mode);
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Set net mode error {}", err);
    return false;
  }

  // Enabling full functionality
  SendATCommand("AT+CFUN=1");
  if (auto err = CheckResponse("OK", 1000, "AT+CFUN command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CFUN command error {}", err);
    return false;
  }

  // Check Sim card
  auto sim_err = CheckSimStatus();
  if ((sim_err == kModemError::kNoError) && modem_init.use_pin) {
    if (auto err = SetupSim(modem_init.pin); err != kModemError::kNoError) {
      AE_TELED_ERROR("Setup sim error {}", err);
      return false;
    }
  }

  if (auto err = SetupNetwork(modem_init.operator_name,
                              modem_init.operator_code, modem_init.apn_name,
                              modem_init.apn_user, modem_init.apn_pass,
                              modem_init.modem_mode, modem_init.auth_type);
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Setup network error {}", err);
    return false;
  }

  return true;
}

bool Sim7070AtModem::Stop() {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }

  // Reset modem settings correctly
  SendATCommand("ATZ");
  if (auto err = CheckResponse("OK", 1000, "ATZ command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("ATZ command error {}", err);
    return false;
  }

  // Disabling full functionality
  SendATCommand("AT+CFUN=0");
  if (auto err = CheckResponse("OK", 1000, "AT+CFUN command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CFUN command error {}", err);
    return false;
  }

  return true;
}

ConnectionIndex Sim7070AtModem::OpenNetwork(ae::Protocol protocol,
                                            std::string const& host,
                                            std::uint16_t port) {
  if (!serial_->IsOpen()) {
    return kInvalidConnectionIndex;
  }

  ConnectionHandle handle{-1, -1};
  if (protocol == ae::Protocol::kTcp) {
    handle = OpenTcpConnection(host, port);
  } else if (protocol == ae::Protocol::kUdp) {
    handle = OpenUdpConnection();
  }

  if (handle.context_index < 0 && handle.connect_index < 0) {
    return kInvalidConnectionIndex;
  }
  auto connect_index = static_cast<ConnectionIndex>(connect_vec_.size());
  connect_vec_.emplace_back(Sim7070Connection{handle, protocol, host, port});
  return connect_index;
}

void Sim7070AtModem::CloseNetwork(ConnectionIndex connect_index) {
    if (connect_index >= connect_vec_.size()) {
    AE_TELED_ERROR("Connection index overflow");
    return;
  }
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port not open");
    return;
  }
  auto const& connection =
      connect_vec_.at(static_cast<std::size_t>(connect_index));
  
  // AT+CACLOSE=0 // Close TCP/UDP socket 0.
  SendATCommand("AT+CACLOSE=" +
                std::to_string(connection.handle.connect_index));
  if (auto err = CheckResponse("OK", 1000, "AT+CACLOSE command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CACLOSE command error {}", err);
    return;
  }
  
  // AT+CNACT=0,0 // Deactivate the PDP context
  SendATCommand("AT+CNACT=" + std::to_string(connection.handle.connect_index) + ",0");
  if (auto err = CheckResponse("OK", 10000, "AT+CNACT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CNACT command error {}", err);
    return;
  }
}

void Sim7070AtModem::WritePacket(ConnectionIndex connect_index,
                                 DataBuffer const& data) {
  if (connect_index >= connect_vec_.size()) {
    AE_TELED_ERROR("Connection index overflow");
    return;
  }
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port not open");
    return;
  }
  if (data.size() > kModemMTU) {
    assert(false);
    return;
  }

  auto const& connection =
      connect_vec_.at(static_cast<std::size_t>(connect_index));

  if (connection.protocol == ae::Protocol::kTcp) {
    SendTcp(connection, data);
  } else if (connection.protocol == ae::Protocol::kUdp) {
    SendUdp(connection, data);
  }
}

DataBuffer Sim7070AtModem::ReadPacket(ConnectionIndex connect_index,
                        Duration timeout) {
  if (connect_index >= connect_vec_.size()) {
    AE_TELED_ERROR("Connection index overflow");
    return {};
  } else if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return {};
  }

  auto const& connection =
      connect_vec_.at(static_cast<std::size_t>(connect_index));

  if (connection.protocol == ae::Protocol::kTcp) {
    return ReadTcp(connection, timeout);
  }
  if (connection.protocol == ae::Protocol::kUdp) {
    return ReadUdp(connection, timeout);
  }
  return {};
}

bool Sim7070AtModem::SetPowerSaveParam(ae::PowerSaveParam const& /*psp*/){
  
  return true;
}

bool Sim7070AtModem::PowerOff() {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }
  // Disabling full functionality
  SendATCommand("AT+CPOWD=1");
  if (auto err = CheckResponse("OK", 1000, "AT+CPOWD command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CPOWD command error {}", err);
    return false;
  }
  
  return true;
}

//=============================private members================================//
kModemError Sim7070AtModem::CheckResponse(std::string response,
                                          std::uint32_t wait_time,
                                          std::string error_message) {
  kModemError err{kModemError::kNoError};

  if (!WaitForResponse(response, std::chrono::milliseconds(wait_time))) {
    AE_TELE_ERROR(kAdapterModemAtError, error_message);
    err = kModemError::kAtCommandError;
  }

  return err;
}

kModemError Sim7070AtModem::SetBaudRate(kBaudRate rate) {
  kModemError err{kModemError::kNoError};

  auto it = baud_rate_commands_sim7070.find(rate);
  if (it == baud_rate_commands_sim7070.end()) {
    err = kModemError::kBaudRateError;
    return err;
  } else {
    SendATCommand(it->second);
  }

  err = CheckResponse("OK", 1000, "No response from modem!");
  if (err != kModemError::kNoError) {
    err = kModemError::kBaudRateError;
  }

  return err;
}

kModemError Sim7070AtModem::CheckSimStatus() {
  kModemError err{kModemError::kNoError};

  SendATCommand("AT+CPIN?");  // Check SIM card status
  err = CheckResponse("OK", 1000, "SIM card error!");

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
  err = CheckResponse("OK", 1000, "SIM card PIN error!");

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

  err = CheckResponse("OK", 1000, "No response from modem!");
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

  err = CheckResponse("OK", 120000, "No response from modem!");
  if (err == kModemError::kNoError) {
    SendATCommand("AT+CGDCONT=0,\"IP\",\"" + apn_name + "\"");
    err = CheckResponse("OK", 1000, "No response from modem!");
  }

  if (err == kModemError::kNoError) {
    SendATCommand("AT+CNCFG=0,0,\"" + apn_name + "\",\"" + apn_user + "\",\"" +
                  apn_pass + "\"," + type);
    err = CheckResponse("OK", 1000, "No response from modem!");
  }

  if (err == kModemError::kNoError) {
    SendATCommand("AT+CREG=1;+CGREG=1;+CEREG=1");
    err = CheckResponse("OK", 1000, "No response from modem!");
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

ConnectionHandle Sim7070AtModem::OpenTcpConnection(std::string const& /* host*/,
                                                   std::uint16_t /* port*/) {
  ConnectionHandle handle{};

  return handle;
}

ConnectionHandle Sim7070AtModem::OpenUdpConnection(){
  ConnectionHandle handle{};
  
  return handle;
}

void Sim7070AtModem::SendTcp(Sim7070Connection const& /* connection */,
                             DataBuffer const& /* data */) {
  
}

void Sim7070AtModem::SendUdp(Sim7070Connection const& /* connection */,
                             DataBuffer const& /* data */) {
  
}

DataBuffer Sim7070AtModem::ReadTcp(Sim7070Connection const& /* connection */,
                                   Duration /* timeout */) {
  DataBuffer response_vector;
  
  return response_vector;
}

DataBuffer Sim7070AtModem::ReadUdp(Sim7070Connection const& /* connection */,
                                   Duration /* timeout */) {
  DataBuffer response_vector;
  
  return response_vector;  
}
  
void Sim7070AtModem::SendATCommand(const std::string& command) {
  AE_TELED_DEBUG("AT command: {}", command);
  std::vector<uint8_t> data(command.begin(), command.end());
  data.push_back('\r');  // Adding a carriage return symbols
  data.push_back('\n');
  serial_->Write(data);
}

bool Sim7070AtModem::WaitForResponse(const std::string& expected,
                                     Duration timeout_ms) {
  // Simplified implementation of waiting for a response
  auto start = std::chrono::high_resolution_clock::now();

  while (true) {
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    if (elapsed > timeout_ms) {
      return false;
    }

    if (auto response = serial_->Read()) {
      std::string response_str(response->begin(), response->end());
      AE_TELED_DEBUG("AT response: {}", response_str);
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
