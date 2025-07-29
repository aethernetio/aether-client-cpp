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
      sendATCommand("AT+CMEE=1");  // Enabling extended errors
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
    // Disabling full functionality
    if (err == kModemError::kNoError) {
      sendATCommand("AT+CFUN=0");
      err = CheckResponce("OK", 1000, "AT+CFUN command error!");
    }

    if (err == kModemError::kNoError) {
      err = SetNetMode(kModemMode::kModeCatMNbIot);
    }

    if (err == kModemError::kNoError) {
      err = SetupNetwork(modem_init_.operator_name, modem_init_.operator_code,
                         modem_init_.apn_name, modem_init_.apn_user,
                         modem_init_.apn_pass, modem_init_.modem_mode,
                         modem_init_.auth_type);
    }

    // Enabling full functionality
    if (err == kModemError::kNoError) {
      sendATCommand("AT+CFUN=1");
      err = CheckResponce("OK", 1000, "AT+CFUN command error!");
      err = CheckResponce("+CEREG: 2", 600000, "AT+CFUN command error!");
      err = CheckResponce("+CEREG: 1", 600000, "AT+CFUN command error!");
    }

    // Check Sim card
    err = CheckSimStatus();
    if (err == kModemError::kNoError && modem_init_.use_pin == true) {
      err = SetupSim(modem_init_.pin);
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
    // Disabling full functionality
    if (err == kModemError::kNoError) {
      //sendATCommand("AT+CFUN=0");
      //err = CheckResponce("OK", 1000, "AT+CFUN command error!");
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

void Thingy91xAtModem::OpenNetwork(std::uint8_t context_index,
                                   std::uint8_t connect_index,
                                   ae::Protocol protocol, std::string host,
                                   std::uint16_t port) {
  std::string context_i_str = std::to_string(context_index);
  std::string connect_i_str = std::to_string(connect_index + 1);
  std::string protocol_str;
  kModemError err{kModemError::kNoError};

  protocol_ = protocol;
  host_ = host;
  port_ = port;

  switch (protocol) {
    case ae::Protocol::kTcp:
      protocol_str = "6";
      break;
    case ae::Protocol::kUdp:
      protocol_str = "17";
      break;
    default:
      err = kModemError::kOpenConnection;
      assert(0);
      break;
  }

  if (serial_->GetConnected()) {
    if (err == kModemError::kNoError) {
      // #XSOCKET=<op>[,<type>,<role>[,<cid>]]
      sendATCommand("AT#XSOCKET=1,2,1");  // Create socket
      err = CheckResponce("OK", 1000, "AT#XSOCKET command error!");
    }
  } else {
    err = kModemError::kSerialPortError;
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Thingy91xAtModem::CloseNetwork(std::uint8_t context_index,
                                    std::uint8_t connect_index) {
  std::string context_i_str = std::to_string(context_index);
  std::string connect_i_str = std::to_string(connect_index + 1);
  kModemError err{kModemError::kNoError};

  if (serial_->GetConnected()) {
    if (err == kModemError::kNoError) {
      sendATCommand("AT#XSOCKET=0," + connect_i_str);  // Close socket
      err = CheckResponce("OK", 1000, "AT#XSOCKET command error!");
    }
  } else {
    err = kModemError::kSerialPortError;
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Thingy91xAtModem::WritePacket(std::uint8_t connect_index,
                                   std::vector<std::uint8_t> const& data) {
  std::string connect_i_str = std::to_string(connect_index);
  kModemError err{kModemError::kNoError};
  std::string protocol_str;
  std::string port_str = std::to_string(port_);

  if (serial_->GetConnected()) {
    if (err == kModemError::kNoError) {
      if (protocol_ == ae::Protocol::kTcp) {
      } else if (protocol_ == ae::Protocol::kUdp) {
        // AT#XSENDTO="172.27.131.100",15683,5,"Dummy"
        // Send TCP/UDP data 0.
        std::string data_string(data.begin(), data.end());
        sendATCommand("AT#XSENDTO=\"" + host_ + "\"," + std::to_string(port_) +
                      ",\"" + data_string + "\"");

        err = CheckResponce("OK", 1000, "AT#XSENDTO command error!");
      }
    }
  } else {
    err = kModemError::kSerialPortError;
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Thingy91xAtModem::ReadPacket(std::uint8_t connect_index,
                                  std::vector<std::uint8_t>& data,
                                  std::size_t& size) {
  std::string connect_i_str = std::to_string(connect_index);
  kModemError err{kModemError::kNoError};

  if (serial_->GetConnected()) {
    if (err == kModemError::kNoError) {
      if (protocol_ == ae::Protocol::kTcp) {
      } else if (protocol_ == ae::Protocol::kUdp) {
        // #XRECVFROM=<timeout>[,<flags>]
        sendATCommand("AT#XRECVFROM=10");
        auto response = serial_->ReadData();
        std::string response_string(response->begin(), response->end());
        auto start = response_string.find("#XRECVFROM: ") + 12;
        auto stop = response_string.find(",");
        if (stop > start && start != std::string::npos &&
            stop != std::string::npos) {
          size = std::stoi(response_string.substr(start, stop - start));
          AE_TELED_DEBUG("Size {}", size);
        } else {
          size = 0;
        }

        if (size > 0) {
          auto start2 = response_string.find(std::to_string(port_)) +
                        std::to_string(port_).size() + 2;
          std::vector<std::uint8_t> response_vector(
              response->begin() + start2, response->begin() + start2 + size);
          data = response_vector;
          AE_TELED_DEBUG("Data {}", data);
        }
      }
    }
  } else {
    err = kModemError::kSerialPortError;
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Thingy91xAtModem::SetPowerSaveParam(kPowerSaveParam const& psp) {
  kModemError err{kModemError::kNoError};

  if (serial_->GetConnected()) {
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

  if (serial_->GetConnected()) {
    // Disabling full functionality
    if (err == kModemError::kNoError) {
      sendATCommand("AT+CFUN=0");
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
    sendATCommand("AT+COPS=1,0,\"" + operator_name + "\"," + mode_str);
  } else if (!operator_code.empty()) {
    // Operator code
    sendATCommand("AT+COPS=1,2,\"" + operator_code + "\"," + mode_str);
  } else {
    // Auto
    sendATCommand("AT+COPS=0");
  }

  err = CheckResponce("OK", 120000, "AT+COPS command error!");
  if (err == kModemError::kNoError) {
    sendATCommand("AT+CGDCONT=0,\"IP\",\"" + apn_name + "\"");
    err = CheckResponce("OK", 1000, "AT+CGDCONT command error!");
  }

  if (err == kModemError::kNoError) {
    sendATCommand("AT+CEREG=1");
    err = CheckResponce("OK", 1000, "AT+CEREG command error!");
  }

  AE_TELED_DEBUG("apn_user", apn_user);
  AE_TELED_DEBUG("apn_pass", apn_pass);
  AE_TELED_DEBUG("auth_type", auth_type);

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
kModemError Thingy91xAtModem::SetPsm(std::uint8_t mode,
                                     kRequestedPeriodicTAUT3412 tau,
                                     kRequestedActiveTimeT3324 active) {
  std::string cmd;
  kModemError err{kModemError::kNoError};

  if (mode == 0) {
    cmd = "AT+CPSMS=" + std::to_string(mode);
  } else if (mode == 1) {
    std::string tau_str =
        std::bitset<8>(((tau.Multiplier << 5) | tau.Value)).to_string();
    std::string active_str =
        std::bitset<8>(((active.Multiplier << 5) | active.Value)).to_string();
    cmd = "AT+CPSMS=" + std::to_string(mode) + ",,,\"" + tau_str + "\",\"" +
          active_str + "\"";
  } else {
    err = kModemError::kSetPsm;
  }

  if (err == kModemError::kNoError) {
    sendATCommand(cmd);
    err = CheckResponce("OK", 2000, "AT+CPSMS command error!");
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
kModemError Thingy91xAtModem::SetEdrx(EdrxMode mode, EdrxActTType act_type,
                                      kEDrx edrx_val) {
  std::string cmd;
  kModemError err{kModemError::kNoError};

  std::string edrx_str = std::bitset<4>((edrx_val.Value)).to_string();
  cmd = "AT+CEDRXS=" + std::to_string(static_cast<std::uint8_t>(mode)) + "," +
        std::to_string(static_cast<std::uint8_t>(act_type)) + ",\"" +
        edrx_str + "\"";

  sendATCommand(cmd);
  err = CheckResponce("OK", 2000, "AT+CPSMS command error!");

  return err;
}

/**
 * Enables/disables Release Assistance Indication (RAI)
 *
 * @param mode         Mode: 0 - disable, 1 - enable, 2 - Activate with
 * unsolicited notifications
 * @return kModemError ErrorCode
 */
kModemError Thingy91xAtModem::SetRai(std::uint8_t mode) {
  std::string cmd;
  kModemError err{kModemError::kNoError};

  if (mode >= 3) {
    err = kModemError::kSetRai;
  } else {
    cmd = "AT%RAI=" + std::to_string(mode);

    sendATCommand(cmd);
    err = CheckResponce("OK", 2000, "AT+CPSMS command error!");
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
    std::uint8_t mode, const std::vector<std::int32_t>& bands) {
  std::string cmd{};
  std::uint64_t band_bit1{0}, band_bit2{0};
  kModemError err{kModemError::kNoError};

  if (mode >= 4) {
    err = kModemError::kSetBandLock;
  } else {
    cmd = "AT%XBANDLOCK=" + std::to_string(mode);

    if (mode == 1 && !bands.empty()) {
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

    sendATCommand(cmd);
    err = CheckResponce("OK", 2000, "AT+CPSMS command error!");
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
kModemError Thingy91xAtModem::ResetModemFactory(std::uint8_t mode) {
  std::string cmd{};
  kModemError err{kModemError::kNoError};

  if (mode >= 2) {
    err = kModemError::kResetMode;
  } else {
    cmd = "AT%XFACTORYRESET=" + std::to_string(mode);

    sendATCommand(cmd);
    err = CheckResponce("OK", 2000, "AT%XFACTORYRESET command error!");
  }

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
