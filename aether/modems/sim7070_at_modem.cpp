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

#include <thread>
#include <chrono>
#include <string_view>

#include "aether/misc/from_chars.h"
#include "aether/modems/exponent_time.h"
#include "aether/modems/sim7070_at_modem.h"
#include "aether/serial_ports/serial_port_factory.h"

#include "aether/modems/modems_tele.h"

namespace ae {

Sim7070AtModem::Sim7070AtModem(ModemInit modem_init)
    : modem_init_{std::move(modem_init)} {
  serial_ = SerialPortFactory::CreatePort(modem_init_.serial_init);
  Init();
  Start();
}

Sim7070AtModem::~Sim7070AtModem() { Stop(); }

bool Sim7070AtModem::Init() {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }

  at_comm_support_->SendATCommand("AT");  // Checking the connection
  if (auto err = CheckResponse("OK", 1000, "AT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT command error {}", err);
    return false;
  }

  at_comm_support_->SendATCommand("ATE0");  // Checking the connection
  if (auto err = CheckResponse("OK", 1000, "ATE0 command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("ATE0 command error {}", err);
    return false;
  }

  at_comm_support_->SendATCommand("AT+CMEE=1");  // Enabling extended errors
  if (auto err = CheckResponse("OK", 1000, "AT+CMEE command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CMEE command error {}", err);
    return false;
  }

  return true;
}

bool Sim7070AtModem::Start() {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }

  // Check Sim card
  auto sim_err = CheckSimStatus();
  if ((sim_err == kModemError::kNoError) && modem_init_.use_pin) {
    if (auto err = SetupSim(modem_init_.pin); err != kModemError::kNoError) {
      AE_TELED_ERROR("Setup sim error {}", err);
      return false;
    }
  }

  // Enabling full functionality
  at_comm_support_->SendATCommand("AT+CFUN=1,0");
  if (auto err = CheckResponse("OK", 1000, "AT+CFUN command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CFUN command error {}", err);
    return false;
  }

  if (auto err = SetBaudRate(modem_init_.serial_init.baud_rate);
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Set baud rate error {}", err);
    return false;
  }

  if (auto err = SetNetMode(modem_init_.modem_mode);
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Set net mode error {}", err);
    return false;
  }

  if (auto err = SetupNetwork(modem_init_.operator_name,
                              modem_init_.operator_code, modem_init_.apn_name,
                              modem_init_.apn_user, modem_init_.apn_pass,
                              modem_init_.modem_mode, modem_init_.auth_type);
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Setup network error {}", err);
    return false;
  }

  return true;
}

bool Sim7070AtModem::Stop() {
  std::string context_i_str{"0"};

  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }

  // AT+CNACT=<pdpidx>,<action> // Deactivate the PDP context
  at_comm_support_->SendATCommand("AT+CNACT=" + context_i_str +
                                  ",0");  // Enabling extended errors
  if (auto err = CheckResponse("+APP PDP: " + context_i_str + ",DEACTIVE", 1000,
                               "AT+CNACT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CNACT command error {}", err);
    return false;
  }

  // Reset modem settings correctly
  at_comm_support_->SendATCommand("ATZ");
  if (auto err = CheckResponse("OK", 1000, "ATZ command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("ATZ command error {}", err);
    return false;
  }

  // Disabling full functionality
  at_comm_support_->SendATCommand("AT+CFUN=0");
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
    handle = OpenUdpConnection(host, port);
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

  // AT+CACLOSE=<cid> // Close TCP/UDP socket 0.
  at_comm_support_->SendATCommand(
      "AT+CACLOSE=" + std::to_string(connection.handle.connect_index));
  if (auto err = CheckResponse("OK", 1000, "AT+CACLOSE command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CACLOSE command error {}", err);
    return;
  }

  connect_vec_.erase(connect_vec_.begin() + connect_index);
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
                                      Duration /* timeout */) {
  if (connect_index >= connect_vec_.size()) {
    AE_TELED_ERROR("Connection index overflow");
    return {};
  }
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return {};
  }

  auto const& connection =
      connect_vec_.at(static_cast<std::size_t>(connect_index));

  if (connection.protocol == ae::Protocol::kTcp) {
    return ReadTcp(connection);
  }
  if (connection.protocol == ae::Protocol::kUdp) {
    return ReadUdp(connection);
  }
  return {};
}

bool Sim7070AtModem::SetPowerSaveParam(ae::PowerSaveParam const& /*psp*/) {
  return true;
}

bool Sim7070AtModem::PowerOff() {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }
  // Disabling full functionality
  at_comm_support_->SendATCommand("AT+CPOWD=1");
  if (auto err = CheckResponse("OK", 1000, "AT+CPOWD command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CPOWD command error {}", err);
    return false;
  }

  return true;
}

// =============================private members=========================== //
kModemError Sim7070AtModem::CheckResponse(std::string const& response,
                                          std::uint32_t const wait_time,
                                          std::string const& error_message) {
  kModemError err{kModemError::kNoError};

  if (!at_comm_support_->WaitForResponse(
          response, std::chrono::milliseconds(wait_time))) {
    AE_TELED_ERROR(error_message);
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
  }

  at_comm_support_->SendATCommand(it->second);
  err = CheckResponse("OK", 1000, "No response from modem!");
  if (err != kModemError::kNoError) {
    err = kModemError::kBaudRateError;
  }

  return err;
}

kModemError Sim7070AtModem::CheckSimStatus() {
  at_comm_support_->SendATCommand("AT+CPIN?");  // Check SIM card status
  auto err = CheckResponse("OK", 1000, "SIM card error!");
  if (err != kModemError::kNoError) {
    err = kModemError::kCheckSimStatus;
  }

  return err;
}

kModemError Sim7070AtModem::SetupSim(const std::uint8_t pin[4]) {
  auto pin_string = at_comm_support_->PinToString(pin);

  if (pin_string == "ERROR") {
    return kModemError::kPinWrong;
  }

  at_comm_support_->SendATCommand("AT+CPIN=" +
                                  pin_string);  // Check SIM card status
  auto err = CheckResponse("OK", 1000, "SIM card PIN error!");
  if (err != kModemError::kNoError) {
    return kModemError::kSetupSim;
  }

  return kModemError::kNoError;
}

kModemError Sim7070AtModem::SetNetMode(kModemMode modem_mode) {
  switch (modem_mode) {
    case kModemMode::kModeAuto:
      at_comm_support_->SendATCommand("AT+CNMP=2");  // Set modem mode Auto
      break;
    case kModemMode::kModeGSMOnly:
      at_comm_support_->SendATCommand("AT+CNMP=13");  // Set modem mode GSMOnly
      break;
    case kModemMode::kModeLTEOnly:
      at_comm_support_->SendATCommand("AT+CNMP=38");  // Set modem mode LTEOnly
      break;
    case kModemMode::kModeGSMLTE:
      at_comm_support_->SendATCommand("AT+CNMP=51");  // Set modem mode GSMLTE
      break;
    case kModemMode::kModeCatM:
      at_comm_support_->SendATCommand("AT+CMNB=1");  // Set modem mode CatM
      break;
    case kModemMode::kModeNbIot:
      at_comm_support_->SendATCommand("AT+CMNB=2");  // Set modem mode NbIot
      break;
    case kModemMode::kModeCatMNbIot:
      at_comm_support_->SendATCommand("AT+CMNB=3");  // Set modem mode CatMNbIot
      break;
    default:
      return kModemError::kSetNetMode;
      break;
  }

  auto err = CheckResponse("OK", 1000, "No response from modem!");
  if (err != kModemError::kNoError) {
    return kModemError::kSetNetMode;
  }

  return kModemError::kNoError;
}

kModemError Sim7070AtModem::SetupNetwork(
    std::string const& operator_name, std::string const& operator_code,
    std::string const& apn_name, std::string const& apn_user,
    std::string const& apn_pass, kModemMode modem_mode, kAuthType auth_type) {
  std::string mode{"0"};
  std::string type{"0"};

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
    at_comm_support_->SendATCommand("AT+COPS=1,0,\"" + operator_name + "\"," +
                                    mode);
  } else if (!operator_code.empty()) {
    // Operator code
    at_comm_support_->SendATCommand("AT+COPS=1,2,\"" + operator_code + "\"," +
                                    mode);
  } else {
    // Auto
    at_comm_support_->SendATCommand("AT+COPS=0");
  }

  if (auto err = CheckResponse("OK", 120000, "No response from modem!");
      err != kModemError::kNoError) {
    return err;
  }
  at_comm_support_->SendATCommand(R"(AT+CGDCONT=1,"IP",")" + apn_name + "\"");
  if (auto err = CheckResponse("OK", 1000, "No response from modem!");
      err != kModemError::kNoError) {
    return err;
  }
  std::string context_i_str = "0";

  // AT+CNCFG=<pdpidx>,<ip_type>,[<APN>,[<usename>,<password>,[<authentication>]]]
  at_comm_support_->SendATCommand("AT+CNCFG=" + context_i_str + ",0,\"" +
                                  apn_name + "\",\"" + apn_user + "\",\"" +
                                  apn_pass + "\"," + type);
  if (auto err = CheckResponse("OK", 1000, "No response from modem!");
      err != kModemError::kNoError) {
    return err;
  }

  at_comm_support_->SendATCommand("AT+CREG=1;+CGREG=1;+CEREG=1");
  if (auto err = CheckResponse("OK", 1000, "No response from modem!");
      err != kModemError::kNoError) {
    return err;
  }

  // AT+CNACT=<pdpidx>,<action> // Activate the PDP context
  at_comm_support_->SendATCommand("AT+CNACT=" + context_i_str + ",1");
  if (auto err = CheckResponse("OK", 1000, "AT+CNACT command error!");
      err != kModemError::kNoError) {
    // AT+CNACT=<pdpidx>,<action> // Deactivate the PDP context
    at_comm_support_->SendATCommand("AT+CNACT=" + context_i_str + ",0");

    if (err = CheckResponse("+APP PDP: " + context_i_str + ",DEACTIVE", 10000,
                            "AT+CNACT command error!");
        err != kModemError::kNoError) {
      return err;
    }

    // AT+CNACT=<pdpidx>,<action> // Activate the PDP context
    at_comm_support_->SendATCommand("AT+CNACT=" + context_i_str + ",1");
    if (err = CheckResponse("OK", 1000, "AT+CNACT command error!");
        err != kModemError::kNoError) {
      return err;
    }
  }
  return kModemError::kNoError;
}

kModemError Sim7070AtModem::SetupProtoPar() {
  kModemError err{kModemError::kNoError};

  // AT+CACFG Set transparent parameters OK
  // AT+CASSLCFG Set SSL parameters OK

  return err;
}

ConnectionHandle Sim7070AtModem::OpenTcpConnection(std::string const& host,
                                                   std::uint16_t port) {
  ConnectionHandle handle{};
  std::string protocol_str{"TCP"};

  AE_TELED_DEBUG("Open tcp connection for {}:{}", host, port);

  if (connect_vec_.size() == 0) {
    handle.context_index = 0;
    handle.connect_index = 0;
  } else {
    auto connection = connect_vec_.back();
    handle.context_index = connection.handle.context_index + 0;
    handle.connect_index = connection.handle.connect_index + 1;
  }

  std::string context_i_str = std::to_string(handle.context_index);
  std::string connect_i_str = std::to_string(handle.connect_index);
  std::string port_str = std::to_string(port);

  // AT+CAOPEN=<cid>,<pdp_index>,<conn_type>,<server>,<port>[,<recv_mode>]
  // AT+CAOPEN=0,0,"TCP","URL",PORT
  at_comm_support_->SendATCommand("AT+CAOPEN=" + connect_i_str + "," +
                                  context_i_str + ",\"" + protocol_str +
                                  "\",\"" + host + "\"," + port_str);
  auto err = CheckResponse("+CAOPEN: " + connect_i_str + ",0", 1000,
                           "AT+CAOPEN command error!");
  if (err != kModemError::kNoError) {
    handle.context_index = -1;
    handle.connect_index = -1;
  }

  return handle;
}

ConnectionHandle Sim7070AtModem::OpenUdpConnection(std::string const& host,
                                                   std::uint16_t port) {
  ConnectionHandle handle{};
  std::string protocol_str{"UDP"};

  AE_TELED_DEBUG("Open udp connection for {}:{}", host, port);

  if (connect_vec_.size() == 0) {
    handle.context_index = 0;
    handle.connect_index = 0;
  } else {
    auto connection = connect_vec_.back();
    handle.context_index = connection.handle.context_index + 0;
    handle.connect_index = connection.handle.connect_index + 1;
  }

  std::string context_i_str = std::to_string(handle.context_index);
  std::string connect_i_str = std::to_string(handle.connect_index);
  std::string port_str = std::to_string(port);

  // AT+CAOPEN=<cid>,<pdp_index>,<conn_type>,<server>,<port>[,<recv_mode>]
  // AT+CAOPEN=0,0,"UDP","URL",PORT
  at_comm_support_->SendATCommand("AT+CAOPEN=" + connect_i_str + "," +
                                  context_i_str + ",\"" + protocol_str +
                                  "\",\"" + host + "\"," + port_str);
  auto err = CheckResponse("+CAOPEN: " + connect_i_str + ",0", 1000,
                           "AT+CAOPEN command error!");
  if (err != kModemError::kNoError) {
    handle.context_index = -1;
    handle.connect_index = -1;
  }

  return handle;
}

void Sim7070AtModem::SendTcp(Sim7070Connection const& connection,
                             DataBuffer const& data) {
  std::string connect_i_str = std::to_string(connection.handle.connect_index);

  // AT+CASEND=<cid>,<datalen>[,<inputtime>]
  // Send TCP/UDP data 0.
  at_comm_support_->SendATCommand("AT+CASEND=" + connect_i_str + "," +
                                  std::to_string(data.size()));  // Send size
  if (auto err = CheckResponse(">", 1000, "AT+CASEND command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CASEND command error {}", err);
    return;
  }

  serial_->Write(data);  // Send data
  if (auto err = CheckResponse("OK", 250, "Write data error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Write data error {}", err);
    return;
  }

  // AT+CAACK=<cid>
  // Query send data information of the TCP/UDP
  // connection with an identifier 0.
  at_comm_support_->SendATCommand("AT+CAACK=" + connect_i_str);
  // +CAACK: <totalsize>,<unacksize> // Total size of sent data is totalsize
  // and unack data is unacksize get data size
  std::ptrdiff_t size = 0;
  auto response = serial_->Read();
  std::string response_string(response->begin(), response->end());
  auto start = response_string.find("+CAACK: ") + 8;
  auto stop = response_string.find(",");
  if (stop > start && start != std::string::npos && stop != std::string::npos) {
    size =
        FromChars<std::ptrdiff_t>(response_string.substr(start, stop - start))
            .value_or(0);
    AE_TELED_DEBUG("Send size {}", size);
  }
}

void Sim7070AtModem::SendUdp(Sim7070Connection const& connection,
                             DataBuffer const& data) {
  std::string connect_i_str = std::to_string(connection.handle.connect_index);

  // AT+CASEND=<cid>,<datalen>[,<inputtime>]
  // Send TCP/UDP data 0.
  at_comm_support_->SendATCommand("AT+CASEND=" + connect_i_str + "," +
                                  std::to_string(data.size()));  // Send size
  if (auto err = CheckResponse(">", 1000, "AT+CASEND command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CASEND command error {}", err);
    return;
  }

  serial_->Write(data);  // Send data
  if (auto err = CheckResponse("OK", 10000, "Write data error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Write data error {}", err);
    return;
  }

  // AT+CAACK=<cid>
  // Query send data information of the TCP/UDP
  // connection with an identifier 0.
  at_comm_support_->SendATCommand("AT+CAACK=" + connect_i_str);
  // +CAACK: <totalsize>,<unacksize> // Total size of sent data is totalsize
  // and unack data is unacksize get data size
  std::ptrdiff_t size = 0;
  auto response = serial_->Read();
  std::string response_string(response->begin(), response->end());
  auto start = response_string.find("+CAACK: ") + 8;
  auto stop = response_string.find(",");
  if (stop > start && start != std::string::npos && stop != std::string::npos) {
    size =
        FromChars<std::ptrdiff_t>(response_string.substr(start, stop - start))
            .value_or(0);
    AE_TELED_DEBUG("Send size {}", size);
  }
}

DataBuffer Sim7070AtModem::ReadTcp(Sim7070Connection const& connection) {
  std::string connect_i_str = std::to_string(connection.handle.connect_index);

  std::int32_t cid{-1};
  std::uint16_t size{0};

  at_comm_support_->SendATCommand("AT+CARECV?");
  auto response = serial_->Read();
  std::string_view response_string{
      reinterpret_cast<char const*>(response->data()), response->size()};
  auto start = response_string.find("+CARECV: ") + 9;
  auto stop = response_string.find(",");
  if (start == std::string_view::npos) {
    return {};
  }
  if ((stop > start) && (start != std::string_view::npos) &&
      (stop != std::string_view::npos)) {
    cid = FromChars<std::int32_t>(response_string.substr(start, stop - start))
              .value_or(0);
    AE_TELED_DEBUG("Cid {}", response_string.substr(start, stop - start));
    AE_TELED_DEBUG("Cid {}", cid);
  }

  start = stop + 1;
  stop = response_string.find("\r\n", 2);
  if ((stop > start) && (start != std::string_view::npos) &&
      (stop != std::string_view::npos)) {
    size = static_cast<std::uint16_t>(
        FromChars<std::ptrdiff_t>(response_string.substr(start, stop - start))
            .value_or(0));
    AE_TELED_DEBUG("Receiving size {}", size);
  }

  if (size > 0 && cid == connection.handle.connect_index) {
    // AT+CARECV=<cid>,<readlen>
    at_comm_support_->SendATCommand("AT+CARECV=" + connect_i_str + "," +
                                    std::to_string(size));
    response = serial_->Read();

    std::string_view response_string2{
        reinterpret_cast<char const*>(response->data()), response->size()};
    auto error = response_string2.find("+CME ERROR:");
    if (error != std::string_view::npos) {
      return {};
    }

    start = response_string2.find("+CARECV: ") + 9;
    stop = response_string2.find(",");

    if ((stop > start) && (start != std::string_view::npos) &&
        (stop != std::string_view::npos)) {
      size = static_cast<std::uint16_t>(
          FromChars<std::ptrdiff_t>(
              response_string2.substr(start, stop - start))
              .value_or(0));
      AE_TELED_DEBUG("Received size {}", size);
    } else {
      return {};
    }

    if (size > 0) {
      auto start2 = static_cast<std::ptrdiff_t>(stop) + 1;
      DataBuffer response_vector(
          response->begin() + start2,
          response->begin() + start2 + static_cast<std::ptrdiff_t>(size));
      AE_TELED_DEBUG("Data {}", response_vector);
      return response_vector;
    }
  }

  return {};
}

DataBuffer Sim7070AtModem::ReadUdp(Sim7070Connection const& connection) {
  std::string connect_i_str = std::to_string(connection.handle.connect_index);

  std::int32_t cid{-1};
  std::uint16_t size{0};

  at_comm_support_->SendATCommand("AT+CARECV?");
  auto response = serial_->Read();
  std::string_view response_string{
      reinterpret_cast<char const*>(response->data()), response->size()};
  auto start = response_string.find("+CARECV: ") + 9;
  auto stop = response_string.find(",");
  if (start == std::string_view::npos) {
    return {};
  }
  if ((stop > start) && (start != std::string_view::npos) &&
      (stop != std::string_view::npos)) {
    cid = FromChars<std::int32_t>(response_string.substr(start, stop - start))
              .value_or(0);
    AE_TELED_DEBUG("Cid {}", response_string.substr(start, stop - start));
    AE_TELED_DEBUG("Cid {}", cid);
  }

  start = stop + 1;
  stop = response_string.find("\r\n", 2);
  if ((stop > start) && (start != std::string_view::npos) &&
      (stop != std::string_view::npos)) {
    size = static_cast<std::uint16_t>(
        FromChars<std::ptrdiff_t>(response_string.substr(start, stop - start))
            .value_or(0));
    AE_TELED_DEBUG("Size {}", response_string.substr(start, stop - start));
    AE_TELED_DEBUG("Size {}", size);
  }

  if (size > 0 && cid == connection.handle.connect_index) {
    // AT+CARECV=<cid>,<readlen>
    at_comm_support_->SendATCommand("AT+CARECV=" + connect_i_str + "," +
                                    std::to_string(size));
    response = serial_->Read();

    std::string_view response_string2{
        reinterpret_cast<char const*>(response->data()), response->size()};
    auto error = response_string2.find("+CME ERROR:");
    if (error != std::string_view::npos) {
      return {};
    }

    start = response_string2.find("+CARECV: ") + 9;
    stop = response_string2.find(",");

    if ((stop > start) && (start != std::string_view::npos) &&
        (stop != std::string_view::npos)) {
      size = static_cast<std::uint16_t>(
          FromChars<std::ptrdiff_t>(
              response_string2.substr(start, stop - start))
              .value_or(0));
      AE_TELED_DEBUG("Size {}", response_string2.substr(start, stop - start));
      AE_TELED_DEBUG("Size {}", size);
    } else {
      return {};
    }

    if (size > 0) {
      auto start2 = static_cast<std::ptrdiff_t>(stop) + 1;
      DataBuffer response_vector(
          response->begin() + start2,
          response->begin() + start2 + static_cast<std::ptrdiff_t>(size));
      AE_TELED_DEBUG("Data {}", response_vector);
      return response_vector;
    }
  }

  return {};
}
} /* namespace ae */
