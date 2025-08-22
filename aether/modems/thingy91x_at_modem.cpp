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

#include <bitset>
#include <thread>

#include "aether/modems/thingy91x_at_modem.h"
#include "aether/serial_ports/serial_port_factory.h"
#include "aether/adapters/adapter_tele.h"

namespace ae {

Thingy91xAtModem::Thingy91xAtModem(ModemInit modem_init, Domain* domain)
    : IModemDriver(modem_init, domain) {
  serial_ = SerialPortFactory::CreatePort(modem_init.serial_init);
};

void Thingy91xAtModem::Init() {
  kModemError err{kModemError::kNoError};

  if (serial_->IsOpen()) {
    if (err == kModemError::kNoError) {
      SendATCommand("AT");  // Checking the connection
      err = CheckResponse("OK", 1000, "AT command error!");
    }
    if (err == kModemError::kNoError) {
      SendATCommand("AT+CMEE=1");  // Enabling extended errors
      err = CheckResponse("OK", 1000, "AT+CMEE command error!");
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
  ModemInit modem_init = GetModemInit();

  if (serial_->IsOpen()) {
    // Disabling full functionality
    if (err == kModemError::kNoError) {
      SendATCommand("AT+CFUN=0");
      err = CheckResponse("OK", 1000, "AT+CFUN command error!");
    }

    if (err == kModemError::kNoError) {
      err = SetNetMode(kModemMode::kModeCatMNbIot);
    }

    if (err == kModemError::kNoError) {
      err = SetupNetwork(modem_init.operator_name, modem_init.operator_code,
                         modem_init.apn_name, modem_init.apn_user,
                         modem_init.apn_pass, modem_init.modem_mode,
                         modem_init.auth_type);
    }

    // Enabling full functionality
    if (err == kModemError::kNoError) {
      SendATCommand("AT+CFUN=1");
      err = CheckResponse("OK", 1000, "AT+CFUN command error!");
      err = CheckResponse("+CEREG: 2", 600000, "AT+CFUN command error!");
      err = CheckResponse("+CEREG: 1", 600000, "AT+CFUN command error!");
    }

    // Check Sim card
    err = CheckSimStatus();
    if (err == kModemError::kNoError && modem_init.use_pin == true) {
      err = SetupSim(modem_init.pin);
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

  if (serial_->IsOpen()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Disabling full functionality
    if (err == kModemError::kNoError) {
      SendATCommand("AT+CFUN=0");
      err = CheckResponse("OK", 1000, "AT+CFUN command error!");
    }

    if (err == kModemError::kNoError) {
      err = ResetModemFactory(1);  // Reset modem settings correctly
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

void Thingy91xAtModem::OpenNetwork(std::int8_t& connect_index,
                                   ae::Protocol const protocol,
                                   std::string const host,
                                   std::uint16_t const port) {
  std::string protocol_str;
  std::int32_t handle{-1};
  Thingy91xConnection conn;
  kModemError err{kModemError::kNoError};

  connect_index = -1;
  protocol_ = protocol;
  host_ = host;
  port_ = port;

  switch (protocol) {
    case ae::Protocol::kTcp:
      protocol_str = "1";
      break;
    case ae::Protocol::kUdp:
      protocol_str = "2";
      break;
    default:
      err = kModemError::kOpenConnection;
      assert(0);
      break;
  }

  if (serial_->IsOpen()) {
    if (err == kModemError::kNoError) {
      if (protocol_ == ae::Protocol::kTcp) {
        // #XSOCKET=<op>[,<type>,<role>[,<cid>]]
        SendATCommand("AT#XSOCKET=1," + protocol_str + ",0");  // Create socket
        auto response = serial_->Read();  // Get socket handle
        std::string response_string(response->begin(), response->end());
        auto start = response_string.find("#XSOCKET: ") + 10;
        auto stop = response_string.find(",");
        if (stop > start && start != std::string::npos &&
            stop != std::string::npos) {
          handle = std::stoi(response_string.substr(start, stop - start));
          AE_TELED_DEBUG("Handle {}", handle);
        } else {
          handle = -1;
        }
        // #XSOCKETSELECT=<handle>
        SendATCommand("AT#XSOCKETSELECT=" +
                      std::to_string(handle));  // Set socket
        err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
        // AT#XSOCKETOPT=1,20,30
        SendATCommand("AT#XSOCKETOPT=1,20,30");  // Set parameters
        err = CheckResponse("OK", 1000, "AT#XSOCKET command error!");
        // AT#XCONNECT="example.com",1234
        SendATCommand("AT#XCONNECT=\"" + host + "\"," +
                      std::to_string(port));  // Connect
        err = CheckResponse("OK", 1000, "AT#XCONNECT command error!");
      } else if (protocol_ == ae::Protocol::kUdp) {
        // #XSOCKET=<op>[,<type>,<role>[,<cid>]]
        SendATCommand("AT#XSOCKET=1," + protocol_str + ",0");  // Create socket
        auto response = serial_->Read();  // Get socket handle
        std::string response_string(response->begin(), response->end());
        auto start = response_string.find("#XSOCKET: ") + 10;
        auto stop = response_string.find(",");
        if (stop > start && start != std::string::npos &&
            stop != std::string::npos) {
          handle = std::stoi(response_string.substr(start, stop - start));
          AE_TELED_DEBUG("Handle {}", handle);
        } else {
          handle = -1;
        }
        // #XSOCKETSELECT=<handle>
        SendATCommand("AT#XSOCKETSELECT=" +
                      std::to_string(handle));  // Set socket
        err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      }

      if (handle >= 0) {
        conn.handle = handle;
        connect_vec_.push_back(conn);
        connect_index = static_cast<std::int8_t>(connect_vec_.size() - 1);
      }
    }
  } else {
    err = kModemError::kSerialPortError;
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Thingy91xAtModem::CloseNetwork(std::int8_t const connect_index) {
  std::int32_t handle{-1};
  kModemError err{kModemError::kNoError};

  if (connect_index >= connect_vec_.size()) {
    err = kModemError::kConnectIndex;
  } else if (!serial_->IsOpen()) {
    err = kModemError::kSerialPortError;
  } else {
    if (err == kModemError::kNoError) {
      if (protocol_ == ae::Protocol::kTcp) {
        handle = connect_vec_.at(connect_index).handle;
        // #XSOCKETSELECT=<handle>
        SendATCommand("AT#XSOCKETSELECT=" +
                      std::to_string(handle));  // Set socket
        err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
        SendATCommand("AT#XSOCKET=0");  // Close socket
        err = CheckResponse("OK", 10000, "AT#XSOCKET command error!");
      } else if (protocol_ == ae::Protocol::kUdp) {
        handle = connect_vec_.at(connect_index).handle;
        // #XSOCKETSELECT=<handle>
        SendATCommand("AT#XSOCKETSELECT=" +
                      std::to_string(handle));  // Set socket
        err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
        SendATCommand("AT#XSOCKET=0");  // Close socket
        err = CheckResponse("OK", 10000, "AT#XSOCKET command error!");
      }
    }
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Thingy91xAtModem::WritePacket(std::int8_t const connect_index,
                                   std::vector<std::uint8_t> const& data) {
  std::int32_t handle{-1};
  std::string protocol_str;
  std::string port_str = std::to_string(port_);
  kModemError err{kModemError::kNoError};

  if (connect_index >= connect_vec_.size()) {
    err = kModemError::kConnectIndex;
  } else if (!serial_->IsOpen()) {
    err = kModemError::kSerialPortError;
  } else if(data.size() > kModemMTU){
    err = kModemError::kDataLength;
    assert((data.size() <= kModemMTU));
  } else {
    if (protocol_ == ae::Protocol::kTcp) {
      handle = connect_vec_.at(static_cast<std::size_t>(connect_index)).handle;
      // #XSOCKETSELECT=<handle>
      SendATCommand("AT#XSOCKETSELECT=" +
                    std::to_string(handle));  // Set socket
      err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      // #XSEND[=<data>]
      // std::string data_string(data.begin(), data.end());
      // sendATCommand("AT#XSEND=\"" + data_string + "\"");
      SendATCommand("AT#XSEND");

      err = CheckResponse("OK", 1000, "AT#XSEND command error!");

      serial_->Write(data);
      SendATCommand("+++");

      err = CheckResponse("#XDATAMODE: 0", 10000, "+++ command error!");
      if (err != kModemError::kNoError) err = kModemError::kXDataMode;

    } else if (protocol_ == ae::Protocol::kUdp) {
      handle = connect_vec_.at(static_cast<std::size_t>(connect_index)).handle;
      // #XSOCKETSELECT=<handle>
      SendATCommand("AT#XSOCKETSELECT=" +
                    std::to_string(handle));  // Set socket
      err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      // #XSENDTO=<url>,<port>[,<data>]
      std::string data_string(data.begin(), data.end());
      SendATCommand("AT#XSENDTO=\"" + host_ + "\"," + std::to_string(port_) +
                    ",\"" + data_string + "\"");

      err = CheckResponse("OK", 1000, "AT#XSENDTO command error!");
    }
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Thingy91xAtModem::ReadPacket(std::int8_t const connect_index,
                                  std::vector<std::uint8_t>& data,
                                  std::int32_t const timeout) {
  std::int32_t handle{-1};
  std::string timeout_str = std::to_string(timeout);
  std::size_t size;
  kModemError err{kModemError::kNoError};

  if (connect_index >= connect_vec_.size()) {
    err = kModemError::kConnectIndex;
  } else if (!serial_->IsOpen()) {
    err = kModemError::kSerialPortError;
  } else {
    if (protocol_ == ae::Protocol::kTcp) {
      handle = connect_vec_.at(static_cast<std::size_t>(connect_index)).handle;
      // #XSOCKETSELECT=<handle>
      SendATCommand("AT#XSOCKETSELECT=" +
                    std::to_string(handle));  // Set socket
      err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      // #XRECV=<timeout>[,<flags>]
      SendATCommand("AT#XRECV=" + timeout_str);
      auto response = serial_->Read();
      std::string response_string(response->begin(), response->end());
      auto start = response_string.find("#XRECV: ") + 8;
      auto stop = response_string.find("\r\n", 2);
      if (stop > start && start != std::string::npos &&
          stop != std::string::npos) {
        size = static_cast<std::size_t>(
            std::stoul(response_string.substr(start, stop - start)));
        AE_TELED_DEBUG("Size {}", size);
      } else {
        size = 0;
      }

      if (size > 0) {
        auto start2 =
            static_cast<std::ptrdiff_t>(response_string.find("\r\n", 2) + 2);
        std::vector<std::uint8_t> response_vector(
            response->begin() + start2,
            response->begin() + start2 + static_cast<std::ptrdiff_t>(size));
        data = response_vector;
        AE_TELED_DEBUG("Data {}", data);
      }
    } else if (protocol_ == ae::Protocol::kUdp) {
      handle = connect_vec_.at(static_cast<std::size_t>(connect_index)).handle;
      // #XSOCKETSELECT=<handle>
      SendATCommand("AT#XSOCKETSELECT=" +
                    std::to_string(handle));  // Set socket
      err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      // #XRECVFROM=<timeout>[,<flags>]
      SendATCommand("AT#XRECVFROM=" + timeout_str);
      auto response = serial_->Read();
      std::string response_string(response->begin(), response->end());
      auto start = response_string.find("#XRECVFROM: ") + 12;
      auto stop = response_string.find(",");
      if (stop > start && start != std::string::npos &&
          stop != std::string::npos) {
        size = static_cast<std::size_t>(
            std::stoul(response_string.substr(start, stop - start)));
        AE_TELED_DEBUG("Size {}", size);
      } else {
        size = 0;
      }

      if (size > 0) {
        auto start2 = static_cast<std::ptrdiff_t>(
            response_string.find(std::to_string(port_)) +
            std::to_string(port_).size() + 2);
        std::vector<std::uint8_t> response_vector(
            response->begin() + start2,
            response->begin() + start2 + static_cast<std::ptrdiff_t>(size));
        data = response_vector;
        AE_TELED_DEBUG("Data {}", data);
      }
    }
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Thingy91xAtModem::PollSockets(std::int8_t const connect_index,
                                   PollResult& results,
                                   std::int32_t const timeout) {
  std::int32_t handle{-1};
  std::string cmd;
  std::int32_t hndl_tst;
  std::vector<PollEvents> revnts{};
  kModemError err{kModemError::kNoError};

  results.connect_index = -1;
  results.revents = revnts;

  if (connect_index >= connect_vec_.size()) {
    err = kModemError::kConnectIndex;
  } else if (!serial_->IsOpen()) {
    err = kModemError::kSerialPortError;
  } else {
    if (err == kModemError::kNoError) {
      handle = connect_vec_.at(static_cast<std::size_t>(connect_index)).handle;
      // #XPOLL=<timeout>[,<handle1>[,<handle2> ...<handle8>]
      cmd = "AT#XPOLL=" + std::to_string(timeout);
      cmd += "," + std::to_string(handle);
      SendATCommand(cmd);
      auto response = serial_->Read();
      std::string response_string(response->begin(), response->end());
      auto start = response_string.find("#XPOLL: ") + 8;
      auto stop = response_string.find(",");
      if (stop > start && start != std::string::npos &&
          stop != std::string::npos) {
        hndl_tst = std::stoi(response_string.substr(start, stop - start));
        AE_TELED_DEBUG("Handle {}", hndl_tst);
        if (hndl_tst == handle) {
          // The  <revents>  value is a hexadecimal string.
          // It represents the returned events, which could be a combination
          // of POLLIN, POLLERR, POLLHUP and POLLNVAL.
          err = ParsePollEvents(response_string.substr(stop + 2, 6), revnts);
          if (err == kModemError::kNoError) {
            results.connect_index = connect_index;
            results.revents = revnts;
          }
        }
      }
    }
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Thingy91xAtModem::SetPowerSaveParam(ae::PowerSaveParam const& psp) {
  kModemError err{kModemError::kNoError};

  if (serial_->IsOpen()) {
    if (err == kModemError::kNoError) {
      SendATCommand("AT+CFUN=0");
      err = CheckResponse("OK", 1000, "AT+CFUN command error!");
    }

    if (err == kModemError::kNoError) {
      // Configure PSM
      err = SetPsm(psp.psm_mode, psp.tau, psp.act);
    }

    if (err == kModemError::kNoError) {
      // Configure eDRX
      err = SetEdrx(psp.edrx_mode, psp.act_type, psp.edrx_val);
    }

    if (err == kModemError::kNoError) {
      // Configure RAI
      err = SetRai(psp.rai_mode);  // Enable RAI
    }

    if (err == kModemError::kNoError) {
      // Configure Band Locking
      err = SetBandLock(psp.bands_mode, psp.bands);  // Unlock all bands
    }

    if (err == kModemError::kNoError) {
      // Configure Band Locking
      err = SetTxPower(psp.modem_mode, psp.power);
      // Set TX power limits
    }

    if (err == kModemError::kNoError) {
      SendATCommand("AT+CFUN=1");
      err = CheckResponse("OK", 1000, "AT+CFUN command error!");
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

void Thingy91xAtModem::PowerOff() {
  kModemError err{kModemError::kNoError};

  if (serial_->IsOpen()) {
    // Disabling full functionality
    if (err == kModemError::kNoError) {
      SendATCommand("AT+CFUN=0");
      err = CheckResponse("OK", 1000, "AT+CFUN command error!");
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

//=============================private members================================//
kModemError Thingy91xAtModem::CheckResponse(std::string const response,
                                            std::uint32_t const wait_time,
                                            std::string const error_message) {
  kModemError err{kModemError::kNoError};

  if (!WaitForResponse(response, std::chrono::milliseconds(wait_time))) {
    AE_TELE_ERROR(kAdapterModemAtError, error_message);
    err = kModemError::kAtCommandError;
  }

  return err;
}

kModemError Thingy91xAtModem::SetBaudRate(std::uint32_t const /*rate*/) {
  kModemError err{kModemError::kNoError};

  return err;
}

kModemError Thingy91xAtModem::CheckSimStatus() {
  kModemError err{kModemError::kNoError};

  SendATCommand("AT+CPIN?");  // Check SIM card status
  err = CheckResponse("OK", 1000, "SIM card error!");
  if (err != kModemError::kNoError) {
    err = kModemError::kCheckSimStatus;
  }

  return err;
}

kModemError Thingy91xAtModem::SetupSim(std::uint8_t const pin[4]) {
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

kModemError Thingy91xAtModem::SetNetMode(kModemMode const modem_mode) {
  kModemError err{kModemError::kNoError};

  switch (modem_mode) {
    case kModemMode::kModeAuto:
      SendATCommand("AT%XSYSTEMMODE=1,1,0,4");  // Set modem mode Auto
      break;
    case kModemMode::kModeCatM:
      SendATCommand("AT%XSYSTEMMODE=1,0,0,1");  // Set modem mode CatM
      break;
    case kModemMode::kModeNbIot:
      SendATCommand("AT%XSYSTEMMODE=0,1,0,2");  // Set modem mode NbIot
      break;
    case kModemMode::kModeCatMNbIot:
      SendATCommand("AT%XSYSTEMMODE=1,1,0,0");  // Set modem mode CatMNbIot
      break;
    default:
      err = kModemError::kSetNetMode;
      return err;
      break;
  }

  return err;
}

kModemError Thingy91xAtModem::SetupNetwork(std::string const operator_name,
                                           std::string const operator_code,
                                           std::string const apn_name,
                                           std::string const apn_user,
                                           std::string const apn_pass,
                                           kModemMode const modem_mode,
                                           kAuthType const auth_type) {
  std::string mode_str;
  kModemError err{kModemError::kNoError};

  switch (modem_mode) {
    case kModemMode::kModeAuto:
      mode_str = "0";  // Set modem mode Auto
      break;
    case kModemMode::kModeCatM:
      mode_str = "8";  // Set modem mode CatM
      break;
    case kModemMode::kModeNbIot:
      mode_str = "9";  // Set modem mode NbIot
      break;
    default:
      mode_str = "0";
      break;
  }

  // Connect to the network
  if (!operator_name.empty()) {
    // Operator long name
    SendATCommand("AT+COPS=1,0,\"" + operator_name + "\"," + mode_str);
  } else if (!operator_code.empty()) {
    // Operator code
    SendATCommand("AT+COPS=1,2,\"" + operator_code + "\"," + mode_str);
  } else {
    // Auto
    SendATCommand("AT+COPS=0");
  }

  err = CheckResponse("OK", 120000, "AT+COPS command error!");
  if (err == kModemError::kNoError) {
    SendATCommand("AT+CGDCONT=0,\"IP\",\"" + apn_name + "\"");
    err = CheckResponse("OK", 1000, "AT+CGDCONT command error!");
  }

  if (err == kModemError::kNoError) {
    SendATCommand("AT+CEREG=1");
    err = CheckResponse("OK", 1000, "AT+CEREG command error!");
  }

  AE_TELED_DEBUG("apn_user {}", apn_user);
  AE_TELED_DEBUG("apn_pass {}", apn_pass);
  AE_TELED_DEBUG("auth_type {}", auth_type);

  return err;
}

kModemError Thingy91xAtModem::SetTxPower(kModemMode const modem_mode,
                                         std::vector<BandPower> const& power) {
  kModemError err{kModemError::kNoError};
  std::string cmd{"AT%XEMPR="};
  std::string bands{};
  std::uint8_t k{0};
  std::uint8_t system_mode{0};
  bool all_bands{false};
  std::int8_t all_bands_power{0};

  if (modem_mode == kModemMode::kModeNbIot) {
    system_mode = 0;
  } else if (modem_mode == kModemMode::kModeCatM) {
    system_mode = 1;
  } else {
    err = kModemError::kSetPwr;
  }

  //%XEMPR=<system_mode>,<k>,<band0>,<pr0>,<band1>,<pr1>,…,<bandk-1>,<prk-1>
  // or
  //%XEMPR=<system_mode>,0,<pr_for_all_bands>
  if (power.size() > 0 && err == kModemError::kNoError) {
    for (auto& pwr : power) {
      if (pwr.band == kModemBand::kALL_BAND) {
        all_bands = true;
        all_bands_power = static_cast<std::int8_t>(pwr.power);
      } else {
        k++;
        bands += "," + std::to_string(static_cast<std::int8_t>(pwr.band)) +
                 "," + std::to_string(static_cast<std::int8_t>(pwr.power));
      }
    }

    if (all_bands) {
      cmd +=
          std::to_string(system_mode) + ",0," + std::to_string(all_bands_power);
    } else {
      cmd +=
          std::to_string(system_mode) + "," + std::to_string(k) + "," + bands;
    }
  } else {
    err = kModemError::kSetPwr;
  }

  if (err == kModemError::kNoError) {
    SendATCommand(cmd);
    err = CheckResponse("OK", 1000, "AT%XEMPR command error!");
  }

  return err;
}

kModemError Thingy91xAtModem::GetTxPower(kModemMode const modem_mode,
                                         std::vector<BandPower>& power) {
  kModemError err{kModemError::kNoError};

  AE_TELED_DEBUG("modem_mode {}", modem_mode);
  for (auto& pwr : power) {
    AE_TELED_DEBUG("power band {} power power {}", pwr.band, pwr.power);
  }

  return err;
}

/**
 * Sets Power Saving Mode (PSM) parameters
 *
 * @param mode         Mode: 0 - disable, 1 - enable
 * @param tau          Requested Periodic TAU in seconds. Use Multiplier 7 to
 * skip
 * @param active       Requested Active Time in seconds. Use Multiplier 7 to
 * skip
 * @return kModemError ErrorCode
 */
kModemError Thingy91xAtModem::SetPsm(
    std::uint8_t const psm_mode, kRequestedPeriodicTAUT3412 const psm_tau,
    kRequestedActiveTimeT3324 const psm_active) {
  std::string cmd;
  kModemError err{kModemError::kNoError};

  if (psm_mode == 0) {
    cmd = "AT+CPSMS=0";
  } else if (psm_mode == 1) {
    std::string tau_str =
        std::bitset<8>(static_cast<unsigned long long>((psm_tau.bits.Multiplier << 5) | psm_tau.bits.Value))
            .to_string();
    std::string active_str = std::bitset<8>(static_cast<unsigned long long>((psm_active.bits.Multiplier << 5) |
                                             psm_active.bits.Value))
                                 .to_string();
    cmd = "AT+CPSMS=" + std::to_string(psm_mode) + ",,,\"" + tau_str + "\",\"" +
          active_str + "\"";
  } else {
    err = kModemError::kSetPsm;
  }

  if (err == kModemError::kNoError) {
    SendATCommand(cmd);
    err = CheckResponse("OK", 2000, "AT+CPSMS command error!");
  }

  return err;
}

/**
 * Configures eDRX (Extended Discontinuous Reception) settings
 *
 * @param mode         Mode: 0 - disable, 1 - enable
 * @param act_type     Network type: 0 = NB-IoT, 1 = LTE-M
 * @param edrx_val     eDRX value in seconds. Use -1 for network default
 * @return kModemError ErrorCode
 */
kModemError Thingy91xAtModem::SetEdrx(EdrxMode const edrx_mode,
                                      EdrxActTType const act_type,
                                      kEDrx const edrx_val) {
  std::string cmd;
  kModemError err{kModemError::kNoError};

  std::string req_edrx_str =
      std::bitset<4>((edrx_val.bits.ReqEDRXValue)).to_string();
  // std::string prov_edrx_str =
  // std::bitset<4>((edrx_val.ProvEDRXValue)).to_string();
  std::string ptw_str = std::bitset<4>((edrx_val.bits.PTWValue)).to_string();

  cmd = "AT+CEDRXS=" + std::to_string(static_cast<std::uint8_t>(edrx_mode)) +
        "," + std::to_string(static_cast<std::uint8_t>(act_type)) + ",\"" +
        req_edrx_str + "\"";

  SendATCommand(cmd);
  err = CheckResponse("OK", 2000, "AT+CPSMS command error!");

  if (err == kModemError::kNoError) {
    cmd = "AT%XPTW=" + std::to_string(static_cast<std::uint8_t>(act_type)) +
          ",\"" + ptw_str + "\"";
    SendATCommand(cmd);
    err = CheckResponse("OK", 2000, "AT%XPTW command error!");
  }

  return err;
}

/**
 * Enables/disables Release Assistance Indication (RAI)
 *
 * @param mode         Mode: 0 - disable, 1 - enable, 2 - Activate with
 * unsolicited notifications
 * @return kModemError ErrorCode
 */
kModemError Thingy91xAtModem::SetRai(std::uint8_t const rai_mode) {
  std::string cmd;
  kModemError err{kModemError::kNoError};

  if (rai_mode >= 3) {
    err = kModemError::kSetRai;
  } else {
    cmd = "AT%RAI=" + std::to_string(rai_mode);

    SendATCommand(cmd);
    err = CheckResponse("OK", 2000, "AT+CPSMS command error!");
  }

  return err;
}

/**
 * Configures LTE band locking
 *
 * @param mode         Operation mode:
 *                     0 - unlock all bands
 *                     1 - lock specified bands
 * @param bands        Vector of band numbers (empty for unlock)
 * @return kModemError ErrorCode
 */
kModemError Thingy91xAtModem::SetBandLock(
    std::uint8_t const bl_mode, const std::vector<std::int32_t>& bands) {
  std::string cmd{};
  std::uint64_t band_bit1{0}, band_bit2{0};
  kModemError err{kModemError::kNoError};

  if (bl_mode >= 4) {
    err = kModemError::kSetBandLock;
  } else {
    cmd = "AT%XBANDLOCK=" + std::to_string(bl_mode);

    if (bl_mode == 1 && !bands.empty()) {
      cmd += ",\"";
      for (const auto& band : bands) {
        if (band < 32) {
          band_bit1 |= 1ULL << band;
        } else {
          band_bit2 |= 1ULL << (band - 32);
        }
      }
      cmd += std::bitset<24>(band_bit2).to_string() +
             std::bitset<64>(band_bit1).to_string() + "\"";
    }

    SendATCommand(cmd);
    err = CheckResponse("OK", 2000, "AT+CPSMS command error!");
  }

  return err;
}

/**
 * @brief Performs a factory reset of the modem using the specified reset mode.
 *
 * This function sends an AT%XFACTORYRESET command with the specified mode
 * parameter to perform a factory reset operation on the modem. The command
 * execution is verified by checking for "OK" response within a 2-second
 * timeout.
 *
 * @param[in] mode Reset operation mode. Valid values are:
 *                 - 1: Soft reset (preserves some settings)
 *                 - 0: Hard reset (full factory defaults)
 *
 * @return kModemError indicating operation status:
 *         - kModemError::kNoError: Reset command executed successfully
 *         - kModemError::kResetMode: Invalid mode parameter (?2)
 *         - Other kModemError codes: Communication error or missing "OK"
 * response
 *
 * @note
 * - The function will return kResetMode error immediately for invalid modes
 * (?2)
 * - Actual modem behavior depends on modem firmware implementation
 * - Hard reset (mode 1) will erase all user configurations
 * - Device may reboot after successful execution
 * - Uses 2000ms response timeout for command verification
 */
kModemError Thingy91xAtModem::ResetModemFactory(std::uint8_t const res_mode) {
  std::string cmd{};
  kModemError err{kModemError::kNoError};

  if (res_mode >= 2) {
    err = kModemError::kResetMode;
  } else {
    cmd = "AT%XFACTORYRESET=" + std::to_string(res_mode);

    SendATCommand(cmd);
    err = CheckResponse("OK", 2000, "AT%XFACTORYRESET command error!");
  }

  return err;
}

/**
 * @brief Parses hexadecimal string into PollEvents flags
 *
 * Processes input strings in "0xXXXX" format, populating a vector
 * with detected event flags. Returns error messages instead of throwing.
 *
 * @param str Input hexadecimal string (e.g., "0x0015")
 * @param[out] events_out Output vector to receive detected flags
 * @return kModemError kNoError string on success, error enum otherwise
 *
 * @par Error Conditions:
 * - Missing "0x" prefix: returns "Missing '0x' prefix"
 * - Invalid hex digit: returns "Invalid hexadecimal digit"
 * - Conversion error: returns "Failed to convert hexadecimal value"
 *
 * @par Example:
 * @code
 *   std::vector<PollEvents> events;
 *   if (auto err = parse_poll_events("0x0005", events); !err.empty()) {
 *       // Handle error
 *   } else {
 *       // Use events: {POLLIN, POLLOUT}
 *   }
 * @endcode
 */
kModemError Thingy91xAtModem::ParsePollEvents(
    const std::string& str, std::vector<PollEvents>& events_out) {
  kModemError err{kModemError::kNoError};

  // Clear output vector before population
  events_out.clear();

  // Validate prefix format
  if (str.size() < 3 || str[0] != '0' || str[1] != 'x') {
    err = kModemError::kPPEMiss0x;
    return err;  // "Missing '0x' prefix";
  }

  // Validate hexadecimal characters
  for (size_t i = 2; i < str.size(); ++i) {
    if (!std::isxdigit(static_cast<unsigned char>(str[i]))) {
      err = kModemError::kPPEInvalidHex;
      return err;  //"Invalid hexadecimal digit";
    }
  }

  // Convert hexadecimal substring
  char* end_ptr = nullptr;
  const std::string hex_part = str.substr(2);
  unsigned long value = std::strtoul(hex_part.c_str(), &end_ptr, 16);

  // Verify complete conversion
  if (end_ptr != hex_part.c_str() + hex_part.size()) {
    err = kModemError::kPPEFailedConvert;
    return err;  // "Failed to convert hexadecimal value";
  }

  // Define all valid flags in standard order
  constexpr std::array VALID_FLAGS{PollEvents::kPOLLIN,  PollEvents::kPOLLPRI,
                                   PollEvents::kPOLLOUT, PollEvents::kPOLLERR,
                                   PollEvents::kPOLLHUP, PollEvents::kPOLLNVAL};

  // Populate output vector with matching flags
  for (const auto flag : VALID_FLAGS) {
    if (value & static_cast<unsigned int>(flag)) {
      events_out.push_back(flag);
    }
  }

  return err;  // Indicate success
}

void Thingy91xAtModem::SendATCommand(const std::string& command) {
  std::vector<uint8_t> data(command.begin(), command.end());
  data.push_back('\r');  // Adding a carriage return symbols
  data.push_back('\n');
  serial_->Write(data);
}

bool Thingy91xAtModem::WaitForResponse(const std::string& expected,
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

std::string Thingy91xAtModem::PinToString(const std::uint8_t pin[4]) {
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
