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
#include <string_view>

#include "aether/format/format.h"
#include "aether/misc/from_chars.h"
#include "aether/modems/exponent_time.h"
#include "aether/modems/thingy91x_at_modem.h"
#include "aether/serial_ports/serial_port_factory.h"

#include "aether/modems/modems_tele.h"

namespace ae {

Thingy91xAtModem::Thingy91xAtModem(ModemInit modem_init)
    : modem_init_{std::move(modem_init)} {
  serial_ = SerialPortFactory::CreatePort(modem_init_.serial_init);
  Init();
  Start();
}

Thingy91xAtModem::~Thingy91xAtModem() { Stop(); }

bool Thingy91xAtModem::Init() {
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

  SendATCommand("AT+CMEE=1");  // Enabling extended errors
  if (auto err = CheckResponse("OK", 1000, "AT+CMEE command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CMEE command error {}", err);
    return false;
  }

  return true;
}

bool Thingy91xAtModem::Start() {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }

  // Disabling full functionality
  SendATCommand("AT+CFUN=0");
  if (auto err = CheckResponse("OK", 1000, "AT+CFUN command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CFUN command error {}", err);
    return false;
  }

  if (auto err = SetNetMode(kModemMode::kModeCatMNbIot);
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

  // Enabling full functionality
  SendATCommand("AT+CFUN=1");
  if (auto err = CheckResponse("OK", 1000, "AT+CFUN command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CFUN command error {}", err);
    return false;
  }

  if (auto err = CheckResponse("+CEREG: 2", 600000, "AT+CFUN command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CFUN command error {}", err);
    return false;
  }

  if (auto err = CheckResponse("+CEREG: 1", 600000, "AT+CFUN command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CFUN command error {}", err);
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

  return true;
}

bool Thingy91xAtModem::Stop() {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }

  // Disabling full functionality
  SendATCommand("AT+CFUN=0");
  if (auto err = CheckResponse("OK", 1000, "AT+CFUN command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CFUN command error {}", err);
    return false;
  }

  if (auto err = ResetModemFactory(1); err != kModemError::kNoError) {
    AE_TELED_ERROR("Reset modem factory error {}", err);
    return false;
  }

  return true;
}

ConnectionIndex Thingy91xAtModem::OpenNetwork(ae::Protocol protocol,
                                              std::string const& host,
                                              std::uint16_t port) {
  if (!serial_->IsOpen()) {
    return kInvalidConnectionIndex;
  }

  std::int32_t handle{-1};
  if (protocol == ae::Protocol::kTcp) {
    handle = OpenTcpConnection(host, port);
  } else if (protocol == ae::Protocol::kUdp) {
    handle = OpenUdpConnection();
  }

  if (handle < 0) {
    return kInvalidConnectionIndex;
  }
  auto connect_index = static_cast<ConnectionIndex>(connect_vec_.size());
  connect_vec_.emplace_back(Thingy91xConnection{handle, protocol, host, port});
  return connect_index;
}

void Thingy91xAtModem::CloseNetwork(ConnectionIndex connect_index) {
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

  // #XSOCKETSELECT=<handle>
  SendATCommand("AT#XSOCKETSELECT=" +
                std::to_string(connection.handle));  // Set socket
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT#XSOCKETSELECT command error {}", err);
    return;
  }
  SendATCommand("AT#XSOCKET=0");  // Close socket
  if (auto err = CheckResponse("OK", 10000, "AT#XSOCKET command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT#XSOCKET command error {}", err);
    return;
  }
}

void Thingy91xAtModem::WritePacket(ConnectionIndex connect_index,
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

DataBuffer Thingy91xAtModem::ReadPacket(ConnectionIndex connect_index,
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

bool Thingy91xAtModem::SetPowerSaveParam(ae::PowerSaveParam const& psp) {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }
  SendATCommand("AT+CFUN=0");
  if (auto err = CheckResponse("OK", 1000, "AT+CFUN command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CFUN command error {}", err);
    return false;
  }

  // Configure eDRX
  if (auto err = SetEdrx(psp.edrx_mode, psp.act_type, psp.edrx_val);
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Set edrx error {}", err);
    return false;
  }

  // Configure RAI
  if (auto err = SetRai(psp.rai_mode); err != kModemError::kNoError) {
    AE_TELED_ERROR("Set rai error {}", err);
    return false;
  }

  // Configure Band Locking
  if (auto err = SetBandLock(psp.bands_mode, psp.bands);  // Unlock all bands
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Set band lock error {}", err);
    return false;
  }

  // Set TX power limits
  if (auto err = SetTxPower(psp.modem_mode, psp.power);
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Set tx power error {}", err);
    return false;
  }
  SendATCommand("AT+CFUN=1");
  if (auto err = CheckResponse("OK", 1000, "AT+CFUN command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT+CFUN command error {}", err);
    return false;
  }
  return true;
}

bool Thingy91xAtModem::PowerOff() {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
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

// ============================private members=============================== //
kModemError Thingy91xAtModem::CheckResponse(std::string const response,
                                            std::uint32_t const wait_time,
                                            std::string const error_message) {
  kModemError err{kModemError::kNoError};

  if (!WaitForResponse(response, std::chrono::milliseconds(wait_time))) {
    AE_TELED_ERROR(error_message);
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

  err = CheckResponse("OK", 1000, "No response from modem!");
  if (err != kModemError::kNoError) {
    err = kModemError::kSetNetMode;
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

  if (err != kModemError::kNoError) {
    err = kModemError::kSetNetwork;
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

  // %XEMPR=<system_mode>,<k>,<band0>,<pr0>,<band1>,<pr1>,…,<bandk-1>,<prk-1>
  // or
  // %XEMPR=<system_mode>,0,<pr_for_all_bands>
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
        std::bitset<8>(static_cast<unsigned long long>(
                           (psm_tau.bits.Multiplier << 5) | psm_tau.bits.Value))
            .to_string();
    std::string active_str =
        std::bitset<8>(
            static_cast<unsigned long long>((psm_active.bits.Multiplier << 5) |
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

std::int32_t Thingy91xAtModem::OpenTcpConnection(std::string const& host,
                                                 std::uint16_t port) {
  AE_TELED_DEBUG("Open tcp connection for {}:{}", host, port);

  // #XSOCKET=<op>[,<type>,<role>[,<cid>]]
  SendATCommand("AT#XSOCKET=1,1,0");  // Create TCP socket
  auto response = serial_->Read();    // Get socket handle
  if (!response) {
    AE_TELED_DEBUG("No response");
    return -1;
  }
  std::string_view response_string{
      reinterpret_cast<char const*>(response->data()), response->size()};
  // find opened handle
  std::int32_t handle{-1};
  auto start = response_string.find("#XSOCKET: ") + 10;
  auto stop = response_string.find(",");
  if ((stop > start) && (start != std::string_view::npos) &&
      (stop != std::string_view::npos)) {
    handle =
        FromChars<std::int32_t>(response_string.substr(start, stop - start))
            .value_or(-1);
    AE_TELED_DEBUG("Handle {}", handle);
  } else {
    AE_TELED_DEBUG("Failed to parser connection handle");
    return -1;
  }

  // #XSOCKETSELECT=<handle>
  SendATCommand("AT#XSOCKETSELECT=" + std::to_string(handle));  // Set socket
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Failed to check opened handle {}", err);
    return -1;
  }
  // AT#XSOCKETOPT=1,20,30
  SendATCommand("AT#XSOCKETOPT=1,20,30");  // Set parameters
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKET command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Failed to set socket parameters {}", err);
    return -1;
  }
  // AT#XCONNECT="example.com",1234
  SendATCommand("AT#XCONNECT=\"" + host + "\"," +
                std::to_string(port));  // Connect
  if (auto err = CheckResponse("OK", 1000, "AT#XCONNECT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Failed to connect to host {}", err);
    return -1;
  }

  return handle;
}

std::int32_t Thingy91xAtModem::OpenUdpConnection() {
  // #XSOCKET=<op>[,<type>,<role>[,<cid>]]
  SendATCommand("AT#XSOCKET=1,2,0");  // Create UDP socket
  auto response = serial_->Read();    // Get socket handle
  std::int32_t handle{-1};
  std::string response_string(response->begin(), response->end());
  auto start = response_string.find("#XSOCKET: ") + 10;
  auto stop = response_string.find(",");
  if (stop > start && start != std::string::npos && stop != std::string::npos) {
    handle =
        FromChars<std::int32_t>(response_string.substr(start, stop - start))
            .value_or(-1);
    AE_TELED_DEBUG("Handle {}", handle);
  } else {
    return -1;
  }
  // #XSOCKETSELECT=<handle>
  SendATCommand("AT#XSOCKETSELECT=" + std::to_string(handle));  // Set socket
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      err != kModemError::kNoError) {
    return -1;
  }
  return handle;
}

void Thingy91xAtModem::SendTcp(Thingy91xConnection const& connection,
                               DataBuffer const& data) {
  DataBuffer terminated_data{data};

  // #XSOCKETSELECT=<handle>
  SendATCommand("AT#XSOCKETSELECT=" +
                std::to_string(connection.handle));  // Set socket
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Select socket error {}", err);
    return;
  }

  // #XSEND[=<data>]
  SendATCommand("AT#XSEND");

  if (auto err = CheckResponse("OK", 1000, "AT#XSEND command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Send data error {}", err);
    return;
  }

  terminated_data.push_back('+');
  terminated_data.push_back('+');
  terminated_data.push_back('+');
  serial_->Write(terminated_data);

  if (auto err = CheckResponse("#XDATAMODE: 0", 10000, "+++ command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Send data error {}", err);
    return;
  }
}

void Thingy91xAtModem::SendUdp(Thingy91xConnection const& connection,
                               DataBuffer const& data) {
  // #XSOCKETSELECT=<handle>
  SendATCommand("AT#XSOCKETSELECT=" +
                std::to_string(connection.handle));  // Set socket
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Send data error {}", err);
    return;
  }
  // #XSENDTO=<url>,<port>[,<data>]
  std::string data_string(data.begin(), data.end());
  SendATCommand(Format(R"(AT#XSENDTO="{}",{},"{}")", connection.host,
                       connection.port, data_string));

  if (auto err = CheckResponse("OK", 1000, "AT#XSENDTO command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Send data error {}", err);
    return;
  }
}

DataBuffer Thingy91xAtModem::ReadTcp(Thingy91xConnection const& connection,
                                     Duration /*timeout*/) {
  // #XSOCKETSELECT=<handle>
  SendATCommand("AT#XSOCKETSELECT=" +
                std::to_string(connection.handle));  // Set socket
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Socket select error {}", err);
    return {};
  }
  // #XRECV=<timeout>[,<flags>]
  SendATCommand("AT#XRECV=0,64");
  auto response = serial_->Read();
  if (!response) {
    AE_TELED_ERROR("Read response empty");
    return {};
  }
  // get data size
  std::ptrdiff_t size = 0;
  std::string_view response_string(
      reinterpret_cast<char const*>(response->data()), response->size());
  auto start = response_string.find("#XRECV: ") + 8;
  auto stop = response_string.find("\r\n", 2);
  if ((stop > start) && (start != std::string_view::npos) &&
      (stop != std::string_view::npos)) {
    size =
        FromChars<std::ptrdiff_t>(response_string.substr(start, stop - start))
            .value_or(0);
    AE_TELED_DEBUG("Size {}", size);
  } else {
    return {};
  }

  if (size > 0) {
    auto start2 = static_cast<std::ptrdiff_t>(stop + 2);
    DataBuffer response_vector(
        response->begin() + start2,
        response->begin() + start2 + static_cast<std::ptrdiff_t>(size));
    AE_TELED_DEBUG("Data {}", response_vector);
    return response_vector;
  }
  return {};
}

DataBuffer Thingy91xAtModem::ReadUdp(Thingy91xConnection const& connection,
                                     Duration /*timeout*/) {
  // #XSOCKETSELECT=<handle>
  SendATCommand("AT#XSOCKETSELECT=" +
                std::to_string(connection.handle));  // Set socket
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Socket select error {}", err);
    return {};
  }
  // #XRECVFROM=<timeout>[,<flags>]
  SendATCommand("AT#XRECVFROM=0,64");
  auto response = serial_->Read();
  if (!response) {
    AE_TELED_ERROR("Read error");
    return {};
  }
  std::ptrdiff_t size = 0;
  std::string_view response_string(
      reinterpret_cast<char const*>(response->data()), response->size());
  auto start = response_string.find("#XRECVFROM: ") + 12;
  auto stop = response_string.find(",");
  if ((stop > start) && (start != std::string_view::npos) &&
      (stop != std::string_view::npos)) {
    size =
        FromChars<std::ptrdiff_t>(response_string.substr(start, stop - start))
            .value_or(0);
    AE_TELED_DEBUG("Size {}", size);
  } else {
    return {};
  }

  if (size > 0) {
    auto port_str = std::to_string(connection.port);
    auto start2 = static_cast<std::ptrdiff_t>(response_string.find(port_str) +
                                              port_str.size() + 2);
    DataBuffer response_vector(response->begin() + start2,
                               response->begin() + start2 + size);
    AE_TELED_DEBUG("Data {}", response_vector);
    return response_vector;
  }
  return {};
}

void Thingy91xAtModem::SendATCommand(const std::string& command) {
  AE_TELED_DEBUG("AT command: {}", command);
  std::vector<uint8_t> data(command.begin(), command.end());
  data.push_back('\r');  // Adding a carriage return symbols
  data.push_back('\n');
  serial_->Write(data);
}

bool Thingy91xAtModem::WaitForResponse(const std::string& expected,
                                       Duration timeout) {
  // Simplified implementation of waiting for a response
  auto start = Now();
  auto exponent_time = ExponentTime(std::chrono::milliseconds{1},
                                    std::chrono::milliseconds{100});

  while (true) {
    if (auto response = serial_->Read(); response) {
      std::string_view response_str(
          reinterpret_cast<char const*>(response->data()), response->size());
      AE_TELED_DEBUG("AT response: {}", response_str);
      if (response_str.find(expected) != std::string::npos) {
        return true;
      }
      if (response_str.find("ERROR") != std::string::npos) {
        return false;
      }
    }

    auto elapsed = Now() - start;
    if (elapsed > timeout) {
      return false;
    }

    // sleep for some time while waiting for response but no more than timeout
    auto sleep_time = exponent_time.Next();
    sleep_time = (elapsed + sleep_time > timeout)
                     ? std::chrono::duration_cast<Duration>(timeout - elapsed)
                     : sleep_time;
    std::this_thread::sleep_for(sleep_time);
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
