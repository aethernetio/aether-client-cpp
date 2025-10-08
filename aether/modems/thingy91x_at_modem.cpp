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

#include "aether/modems/thingy91x_at_modem.h"

#include <bitset>
#include <string_view>

#include "aether/format/format.h"
#include "aether/misc/from_chars.h"
#include "aether/actions/pipeline.h"
#include "aether/actions/gen_action.h"
#include "aether/actions/failed_action.h"
#include "aether/serial_ports/serial_port_factory.h"

#include "aether/modems/modems_tele.h"

namespace ae {

Thingy91xAtModem::Thingy91xAtModem(ActionContext action_context,
                                   IPoller::ptr poller, ModemInit modem_init)
    : action_context_{action_context},
      modem_init_{std::move(modem_init)},
      serial_{SerialPortFactory::CreatePort(action_context_, std::move(poller),
                                            modem_init_.serial_init)},
      at_comm_support_{action_context_, *serial_} {
  Init();
  Start();
}

Thingy91xAtModem::~Thingy91xAtModem() { Stop(); }

ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::Init() {
  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this]() { return at_comm_support_.SendATCommand("AT"); }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }),
      Stage([this]() { return at_comm_support_.SendATCommand("AT+CMEE=1"); }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
}

ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::Start() {
  return MakeActionPtr<Pipeline>(
      action_context_,
      // Disabling full functionality
      Stage([this]() { return at_comm_support_.SendATCommand("AT+CFUN=0"); }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }),
      Stage([this]() { return SetNetMode(kModemMode::kModeCatMNbIot); }),
      Stage([this]() {
        return SetupNetwork(modem_init_.operator_name,
                            modem_init_.operator_code, modem_init_.apn_name,
                            modem_init_.apn_user, modem_init_.apn_pass,
                            modem_init_.modem_mode, modem_init_.auth_type);
      }),
      // Enabling full functionality
      Stage([this]() { return at_comm_support_.SendATCommand("AT+CFUN=1"); }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse("+CEREG: 2",
                                                std::chrono::seconds{60});
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse("+CEREG: 1",
                                                std::chrono::seconds{60});
      }),
      Stage([this]() { return CheckSimStatus(); }),
      Stage([this]() -> ActionPtr<ModemOperation> {
        if (modem_init_.use_pin) {
          return SetupSim(modem_init_.pin);
        }
        // ignore setup sim
        return MakeActionPtr<Pipeline>(action_context_,
                                       Stage<GenAction>(action_context_, []() {
                                         return UpdateStatus::Result();
                                       }));
      }));
}

ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::Stop() {
  return MakeActionPtr<Pipeline>(
      action_context_,
      // Disabling full functionality
      Stage([this]() { return at_comm_support_.SendATCommand("AT+CFUN=0"); }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }),
      Stage([this]() { return ResetModemFactory(1); }));
}

ActionPtr<Thingy91xAtModem::OpenNetworkOperation> Thingy91xAtModem::OpenNetwork(
    ae::Protocol protocol, std::string const& host, std::uint16_t port) {
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
  at_comm_support_->SendATCommand(
      "AT#XSOCKETSELECT=" + std::to_string(connection.handle));  // Set socket
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT#XSOCKETSELECT command error {}", err);
    return;
  }
  at_comm_support_->SendATCommand("AT#XSOCKET=0");  // Close socket
  if (auto err = CheckResponse("OK", 10000, "AT#XSOCKET command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("AT#XSOCKET command error {}", err);
    return;
  }

  connect_vec_.erase(connect_vec_.begin() + connect_index);
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

ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::SetPowerSaveParam(
    ae::PowerSaveParam const& psp) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this]() { return at_comm_support_.SendATCommand("AT+CFUN=0"); }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }),
      // Configure eDRX
      Stage([this, psp]() {
        return SetEdrx(psp.edrx_mode, psp.act_type, psp.edrx_val);
      }),
      // Configure RAI
      Stage([this, psp]() { return SetRai(psp.rai_mode); }),
      // Configure Band Locking
      Stage([this, psp]() { return SetBandLock(psp.bands_mode, psp.bands); }),
      // Set TX power limits
      Stage([this, psp]() { return SetTxPower(psp.modem_mode, psp.power); }),
      Stage([this]() { return at_comm_support_.SendATCommand("AT+CFUN=1"); }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
}

bool Thingy91xAtModem::PowerOff() {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
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

// ============================private members=============================== //
ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::CheckSimStatus() {
  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this]() { return at_comm_support_.SendATCommand("AT+CPIN?"); }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
}

ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::SetupSim(
    std::uint16_t pin) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this, pin]() -> ActionPtr<AtCommSupport::WriteAction> {
        auto pin_string = at_comm_support_.PinToString(pin);

        if (pin_string.empty()) {
          AE_TELED_ERROR("Pin wrong!");
          return {};
        }
        // Check SIM card status
        return at_comm_support_.SendATCommand("AT+CPIN=" + pin_string);
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
}

ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::SetNetMode(
    kModemMode const modem_mode) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this, modem_mode]() -> ActionPtr<AtCommSupport::WriteAction> {
        switch (modem_mode) {
          case kModemMode::kModeAuto:
            return at_comm_support_.SendATCommand(
                "AT%XSYSTEMMODE=1,1,0,4");  // Set modem mode Auto
          case kModemMode::kModeCatM:
            return at_comm_support_.SendATCommand(
                "AT%XSYSTEMMODE=1,0,0,1");  // Set modem mode CatM
          case kModemMode::kModeNbIot:
            return at_comm_support_.SendATCommand(
                "AT%XSYSTEMMODE=0,1,0,2");  // Set modem mode NbIot
          case kModemMode::kModeCatMNbIot:
            return at_comm_support_.SendATCommand(
                "AT%XSYSTEMMODE=1,1,0,0");  // Set modem mode CatMNbIot
          default:
            return {};
        }
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
}

ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::SetupNetwork(
    std::string const& operator_name, std::string const& operator_code,
    std::string const& apn_name, std::string const& apn_user,
    std::string const& apn_pass, kModemMode modem_mode, kAuthType auth_type) {
  AE_TELED_DEBUG("\n apn_user {}\n apn_pass {}\n auth_type {}", apn_user,
                 apn_pass, auth_type);

  std::string mode_str = std::invoke([&] {
    switch (modem_mode) {
      case kModemMode::kModeAuto:
        return "0";  // Set modem mode Auto
        break;
      case kModemMode::kModeCatM:
        return "8";  // Set modem mode CatM
        break;
      case kModemMode::kModeNbIot:
        return "9";  // Set modem mode NbIot
        break;
      default:
        return "0";
        break;
    }
  });

  return MakeActionPtr<Pipeline>(
      action_context_,
      //
      Stage([this, operator_name, mode_str, operator_code]() {
        std::string cmd;
        // Connect to the network
        if (!operator_name.empty()) {
          // Operator long name
          cmd = "AT+COPS=1,0,\"" + operator_name + "\"," + mode_str;
        } else if (!operator_code.empty()) {
          // Operator code
          cmd = "AT+COPS=1,2,\"" + operator_code + "\"," + mode_str;
        } else {
          // Auto
          cmd = "AT+COPS=0";
        }
        return at_comm_support_.SendATCommand(cmd);
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse("OK",
                                                std::chrono::seconds{120});
      }),
      Stage([this, apn_name]() {
        return at_comm_support_.SendATCommand("AT+CGDCONT=0,\"IP\",\"" +
                                              apn_name + "\"");
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }),
      Stage([this, apn_name]() {
        return at_comm_support_.SendATCommand("AT+CEREG=1,\"IP\",\"" +
                                              apn_name + "\"");
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
}

ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::SetTxPower(
    kModemMode const modem_mode, std::vector<BandPower> const& power) {
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
    return {};
  }

  // %XEMPR=<system_mode>,<k>,<band0>,<pr0>,<band1>,<pr1>,…,<bandk-1>,<prk-1>
  // or
  // %XEMPR=<system_mode>,0,<pr_for_all_bands>
  if (power.size() > 0) {
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
    return {};
  }

  return MakeActionPtr<Pipeline>(
      action_context_,  //
      Stage([this, cmd]() { return at_comm_support_.SendATCommand(cmd); }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
}

ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::GetTxPower(
    kModemMode const modem_mode, std::vector<BandPower>& power) {
  AE_TELED_DEBUG("modem_mode {}", modem_mode);
  for (auto& pwr : power) {
    AE_TELED_DEBUG("power band {} power power {}", pwr.band, pwr.power);
  }

  // TODO: not implemented
  return MakeActionPtr<Pipeline>(action_context_,
                                 Stage<GenAction>(action_context_, []() {
                                   return UpdateStatus::Result();
                                 }));
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
ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::SetPsm(
    std::uint8_t const psm_mode, kRequestedPeriodicTAUT3412 const psm_tau,
    kRequestedActiveTimeT3324 const psm_active) {
  if (psm_mode > 1) {
    return {};
  }

  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, psm_mode, psm_tau, psm_active]() {
        std::string cmd;
        if (psm_mode == 0) {
          cmd = "AT+CPSMS=0";
        } else if (psm_mode == 1) {
          std::string tau_str =
              std::bitset<8>(
                  static_cast<unsigned long long>(
                      (psm_tau.bits.Multiplier << 5) | psm_tau.bits.Value))
                  .to_string();
          std::string active_str =
              std::bitset<8>(static_cast<unsigned long long>(
                                 (psm_active.bits.Multiplier << 5) |
                                 psm_active.bits.Value))
                  .to_string();
          cmd = "AT+CPSMS=" + std::to_string(psm_mode) + ",,,\"" + tau_str +
                "\",\"" + active_str + "\"";
        }
        return at_comm_support_.SendATCommand(cmd);
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{1000});
      }));
}

/**
 * Configures eDRX (Extended Discontinuous Reception) settings
 *
 * @param mode         Mode: 0 - disable, 1 - enable
 * @param act_type     Network type: 0 = NB-IoT, 1 = LTE-M
 * @param edrx_val     eDRX value in seconds. Use -1 for network default
 * @return kModemError ErrorCode
 */
ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::SetEdrx(
    EdrxMode const edrx_mode, EdrxActTType const act_type,
    kEDrx const edrx_val) {
  std::string req_edrx_str =
      std::bitset<4>((edrx_val.bits.ReqEDRXValue)).to_string();
  // std::string prov_edrx_str =
  // std::bitset<4>((edrx_val.ProvEDRXValue)).to_string();
  std::string ptw_str = std::bitset<4>((edrx_val.bits.PTWValue)).to_string();

  std::string cmd =
      "AT+CEDRXS=" + std::to_string(static_cast<std::uint8_t>(edrx_mode)) +
      "," + std::to_string(static_cast<std::uint8_t>(act_type)) + ",\"" +
      req_edrx_str + "\"";

  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this, cmd]() { return at_comm_support_.SendATCommand(cmd); }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{2000});
      }),
      Stage([this, ptw_str, act_type]() {
        std::string cmd =
            "AT%XPTW=" + std::to_string(static_cast<std::uint8_t>(act_type)) +
            ",\"" + ptw_str + "\"";
        return at_comm_support_.SendATCommand(cmd);
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{2000});
      }));
}

/**
 * Enables/disables Release Assistance Indication (RAI)
 *
 * @param mode         Mode: 0 - disable, 1 - enable, 2 - Activate with
 * unsolicited notifications
 * @return kModemError ErrorCode
 */
ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::SetRai(
    std::uint8_t const rai_mode) {
  std::string cmd;
  if (rai_mode >= 3) {
    return {};
  }

  return MakeActionPtr<Pipeline>(action_context_,  //
                                 Stage([this, rai_mode]() {
                                   std::string cmd =
                                       "AT%RAI=" + std::to_string(rai_mode);
                                   return at_comm_support_.SendATCommand(cmd);
                                 }),
                                 Stage([this]() {
                                   return at_comm_support_.WaitForResponse(
                                       "OK", std::chrono::milliseconds{2000});
                                 }));
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
ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::SetBandLock(
    std::uint8_t const bl_mode, const std::vector<std::int32_t>& bands) {
  std::uint64_t band_bit1{0}, band_bit2{0};

  if (bl_mode >= 4) {
    return {};
  }
  std::string cmd = "AT%XBANDLOCK=" + std::to_string(bl_mode);

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

  return MakeActionPtr<Pipeline>(action_context_, Stage([this, cmd]() {
                                   return at_comm_support_.SendATCommand(cmd);
                                 }),
                                 Stage([this]() {
                                   return at_comm_support_.WaitForResponse(
                                       "OK", std::chrono::milliseconds{2000});
                                 }));
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
ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::ResetModemFactory(
    std::uint8_t res_mode) {
  if (res_mode >= 2) {
    return MakeActionPtr<Pipeline>(action_context_,
                                   Stage<FailedAction>(action_context_));
  }
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this]() {
        return at_comm_support_.SendATCommand("AT%XFACTORYRESET=" +
                                              std::to_string(res_mode));
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(
            "OK", std::chrono::milliseconds{2000});
      }));
}

std::int32_t Thingy91xAtModem::OpenTcpConnection(std::string const& host,
                                                 std::uint16_t port) {
  AE_TELED_DEBUG("Open tcp connection for {}:{}", host, port);

  // #XSOCKET=<op>[,<type>,<role>[,<cid>]]
  at_comm_support_->SendATCommand("AT#XSOCKET=1,1,0");  // Create TCP socket
  auto response = serial_->Read();                      // Get socket handle
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
  at_comm_support_->SendATCommand("AT#XSOCKETSELECT=" +
                                  std::to_string(handle));  // Set socket
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Failed to check opened handle {}", err);
    return -1;
  }
  // AT#XSOCKETOPT=1,20,30
  at_comm_support_->SendATCommand("AT#XSOCKETOPT=1,20,30");  // Set parameters
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKET command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Failed to set socket parameters {}", err);
    return -1;
  }
  // AT#XCONNECT="example.com",1234
  at_comm_support_->SendATCommand("AT#XCONNECT=\"" + host + "\"," +
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
  at_comm_support_->SendATCommand("AT#XSOCKET=1,2,0");  // Create UDP socket
  auto response = serial_->Read();                      // Get socket handle
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
  at_comm_support_->SendATCommand("AT#XSOCKETSELECT=" +
                                  std::to_string(handle));  // Set socket
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
  at_comm_support_->SendATCommand(
      "AT#XSOCKETSELECT=" + std::to_string(connection.handle));  // Set socket
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Select socket error {}", err);
    return;
  }

  // #XSEND[=<data>]
  at_comm_support_->SendATCommand("AT#XSEND");

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
  at_comm_support_->SendATCommand(
      "AT#XSOCKETSELECT=" + std::to_string(connection.handle));  // Set socket
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Send data error {}", err);
    return;
  }
  // #XSENDTO=<url>,<port>[,<data>]
  std::string data_string(data.begin(), data.end());
  at_comm_support_->SendATCommand(Format(R"(AT#XSENDTO="{}",{},"{}")",
                                         connection.host, connection.port,
                                         data_string));

  if (auto err = CheckResponse("OK", 1000, "AT#XSENDTO command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Send data error {}", err);
    return;
  }
}

DataBuffer Thingy91xAtModem::ReadTcp(Thingy91xConnection const& connection,
                                     Duration /*timeout*/) {
  // #XSOCKETSELECT=<handle>
  at_comm_support_->SendATCommand(
      "AT#XSOCKETSELECT=" + std::to_string(connection.handle));  // Set socket
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Socket select error {}", err);
    return {};
  }
  // #XRECV=<timeout>[,<flags>]
  at_comm_support_->SendATCommand("AT#XRECV=0,64");
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
  at_comm_support_->SendATCommand(
      "AT#XSOCKETSELECT=" + std::to_string(connection.handle));  // Set socket
  if (auto err = CheckResponse("OK", 1000, "AT#XSOCKETSELECT command error!");
      err != kModemError::kNoError) {
    AE_TELED_ERROR("Socket select error {}", err);
    return {};
  }
  // #XRECVFROM=<timeout>[,<flags>]
  at_comm_support_->SendATCommand("AT#XRECVFROM=0,64");
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
} /* namespace ae */
