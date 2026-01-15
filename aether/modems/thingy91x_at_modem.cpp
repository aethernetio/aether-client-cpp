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
#if AE_SUPPORT_MODEMS && AE_ENABLE_THINGY91X

#  include <bitset>
#  include <string_view>

#  include "aether/misc/from_chars.h"
#  include "aether/actions/pipeline.h"
#  include "aether/actions/gen_action.h"
#  include "aether/serial_ports/serial_port_factory.h"

#  include "aether/modems/modems_tele.h"

namespace ae {
static constexpr Duration kOneSecond = std::chrono::milliseconds{1000};
static constexpr Duration kTwoSeconds = std::chrono::milliseconds{2000};
static constexpr Duration kTenSeconds = std::chrono::milliseconds{10000};
static const AtRequest::Wait kWaitOk{"OK", kOneSecond};
static const AtRequest::Wait kWaitOkTwoSeconds{"OK", kTwoSeconds};

class Thingy91TcpOpenNetwork final : public Action<Thingy91TcpOpenNetwork> {
 public:
  Thingy91TcpOpenNetwork(ActionContext action_context, Thingy91xAtModem& modem,
                         std::string host, std::uint16_t port)
      : Action{action_context},
        action_context_{action_context},
        modem_{&modem},
        at_comm_support_{&modem.at_comm_support_},
        host_{std::move(host)},
        port_{port} {
    AE_TELED_DEBUG("Open tcp connection for {}:{}", host_, port_);
    operation_pipeline_ = MakeActionPtr<Pipeline>(
        action_context_,
        // #XSOCKET=<op>[,<type>,<role>[,<cid>]]
        Stage([this]() {
          return at_comm_support_->MakeRequest(
              "AT#XSOCKET=1,1,0",
              AtRequest::Wait{
                  "#XSOCKET: ", kTwoSeconds, [this](auto&, auto pos) {
                    // #XSOCKET: <handle>,<type>,<protocol>
                    auto res =
                        AtSupport::ParseResponse(*pos, "#XSOCKET", handle_);
                    if (!res) {
                      AE_TELED_DEBUG("Failed to parser connection handle");
                      handle_ = -1;
                      return false;
                    }
                    AE_TELED_DEBUG("Got socket handle {}", handle_);
                    return true;
                  }});
        }),
        // #XSOCKETSELECT=<handle>
        Stage([this]() {
          return at_comm_support_->MakeRequest(
              "AT#XSOCKETSELECT=" + std::to_string(handle_), kWaitOk);
        }),
        // AT#XSOCKETOPT=1,20,30
        Stage([this]() {
          return at_comm_support_->MakeRequest("AT#XSOCKETOPT=1,20,30",
                                               kWaitOk);
        }),
        // AT#XCONNECT="example.com",1234
        Stage([this]() {
          return at_comm_support_->MakeRequest(
              "AT#XCONNECT=\"" + host_ + "\"," + std::to_string(port_),
              kWaitOk);
        }),
        Stage<GenAction>(action_context_, [this]() {
          modem_->connections_.emplace(static_cast<ConnectionIndex>(handle_));
          return UpdateStatus::Result();
        }));

    operation_pipeline_->StatusEvent().Subscribe(ActionHandler{
        OnResult{[this]() {
          success_ = true;
          Action::Trigger();
        }},
        OnError{[this]() {
          error_ = true;
          Action::Trigger();
        }},
        OnStop{[this]() {
          stop_ = true;
          Action::Trigger();
        }},
    });
  }

  UpdateStatus Update() const {
    if (success_) {
      return UpdateStatus::Result();
    }
    if (error_) {
      return UpdateStatus::Error();
    }
    if (stop_) {
      return UpdateStatus::Stop();
    }
    return {};
  }

  ConnectionIndex connection_index() const {
    return static_cast<ConnectionIndex>(handle_);
  }

 private:
  ActionContext action_context_;
  Thingy91xAtModem* modem_;
  AtSupport* at_comm_support_;
  std::string host_;
  std::uint16_t port_;
  ActionPtr<IPipeline> operation_pipeline_;
  Subscription operation_sub_;
  std::int32_t handle_{-1};
  bool success_{};
  bool error_{};
  bool stop_{};
};

class Thingy91UdpOpenNetwork final : public Action<Thingy91UdpOpenNetwork> {
 public:
  Thingy91UdpOpenNetwork(ActionContext action_context, Thingy91xAtModem& modem,
                         std::string host, std::uint16_t port)
      : Action{action_context},
        action_context_{action_context},
        modem_{&modem},
        at_comm_support_{&modem.at_comm_support_},
        host_{std::move(host)},
        port_{port} {
    AE_TELED_DEBUG("Open UDP connection for {}:{}", host_, port_);
    operation_pipeline_ = MakeActionPtr<Pipeline>(
        action_context_,
        // #XSOCKET=<op>[,<type>,<role>[,<cid>]]
        Stage([this]() {
          return at_comm_support_->MakeRequest(
              "AT#XSOCKET=1,2,0",
              AtRequest::Wait{
                  "#XSOCKET: ", kTwoSeconds, [this](auto&, auto pos) {
                    // #XSOCKET: <handle>,<type>,<protocol>
                    auto res =
                        AtSupport::ParseResponse(*pos, "#XSOCKET", handle_);
                    if (!res) {
                      AE_TELED_DEBUG("Failed to parser connection handle");
                      handle_ = -1;
                      return false;
                    }
                    AE_TELED_DEBUG("Got socket handle {}", handle_);
                    return true;
                  }});
        }),
        // #XSOCKETSELECT=<handle>
        Stage([this]() {
          return at_comm_support_->MakeRequest(
              "AT#XSOCKETSELECT=" + std::to_string(handle_), kWaitOk);
        }),
        // AT#XCONNECT="example.com",1234
        Stage([this]() {
          return at_comm_support_->MakeRequest(
              "AT#XCONNECT=\"" + host_ + "\"," + std::to_string(port_),
              kWaitOk);
        }),
        Stage<GenAction>(action_context_, [this]() {
          modem_->connections_.emplace(static_cast<ConnectionIndex>(handle_));
          return UpdateStatus::Result();
        }));

    operation_pipeline_->StatusEvent().Subscribe(ActionHandler{
        OnResult{[this]() {
          success_ = true;
          Action::Trigger();
        }},
        OnError{[this]() {
          error_ = true;
          Action::Trigger();
        }},
        OnStop{[this]() {
          stop_ = true;
          Action::Trigger();
        }},
    });
  }

  UpdateStatus Update() const {
    if (success_) {
      return UpdateStatus::Result();
    }
    if (error_) {
      return UpdateStatus::Error();
    }
    if (stop_) {
      return UpdateStatus::Stop();
    }
    return {};
  }

  ConnectionIndex connection_index() const {
    return static_cast<ConnectionIndex>(handle_);
  }

 private:
  ActionContext action_context_;
  Thingy91xAtModem* modem_;
  AtSupport* at_comm_support_;
  std::string host_;
  std::uint16_t port_;
  ActionPtr<IPipeline> operation_pipeline_;
  Subscription operation_sub_;
  std::int32_t handle_{-1};
  bool success_{};
  bool error_{};
  bool stop_{};
};

Thingy91xAtModem::Thingy91xAtModem(ActionContext action_context,
                                   IPoller::ptr const& poller,
                                   ModemInit modem_init)
    : action_context_{action_context},
      modem_init_{std::move(modem_init)},
      serial_{SerialPortFactory::CreatePort(action_context_, poller,
                                            modem_init_.serial_init)},
      at_comm_support_{action_context_, *serial_},
      operation_queue_{action_context_},
      initiated_{false},
      started_{false} {
  AE_TELED_DEBUG("Thingy91xAtModem init");
  Init();
}

void Thingy91xAtModem::Init() {
  operation_queue_->Push(Stage([this]() {
    auto init_pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        Stage([this]() { return at_comm_support_.MakeRequest("AT", kWaitOk); }),
        Stage([this]() {
          return at_comm_support_.MakeRequest("AT+CMEE=1", kWaitOk);
        }),
        Stage<GenAction>(action_context_, [this]() {
          initiated_ = true;
          return UpdateStatus::Result();
        }));

    init_pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{[]() { AE_TELED_INFO("Thingy91xAtModem init success"); }},
        OnError{[]() { AE_TELED_ERROR("Thingy91xAtModem init failed"); }},
    });

    return init_pipeline;
  }));
}

ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::Start() {
  auto modem_operation = ActionPtr<ModemOperation>{action_context_};
  operation_queue_->Push(Stage([this,
                                modem_operation]() -> ActionPtr<IPipeline> {
    // if already started, notify of success and return
    if (started_) {
      return MakeActionPtr<Pipeline>(
          action_context_,
          Stage<GenAction>(action_context_, [modem_operation]() {
            modem_operation->Notify();
            return UpdateStatus::Result();
          }));
    }

    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        Stage<GenAction>(action_context_,
                         [this]() {
                           if (!initiated_) {
                             return UpdateStatus::Error();
                           }
                           return UpdateStatus::Result();
                         }),
        // Disabling full functionality
        Stage([this]() {
          return at_comm_support_.MakeRequest("AT+CFUN=0", kWaitOk);
        }),
        Stage([this]() { return SetNetMode(kModemMode::kModeCatMNbIot); }),
        Stage([this]() {
          return SetupNetwork(modem_init_.operator_name,
                              modem_init_.operator_code, modem_init_.apn_name,
                              modem_init_.apn_user, modem_init_.apn_pass,
                              modem_init_.modem_mode, modem_init_.auth_type);
        }),
        // Enabling full functionality and waiting for network registration
        Stage([this]() {
          return at_comm_support_.MakeRequest(
              "AT+CFUN=1", kWaitOk,
              AtRequest::Wait{"+CEREG: 2", std::chrono::seconds{60}},
              AtRequest::Wait{"+CEREG: 1", std::chrono::seconds{60}});
        }),
        Stage([this]() { return CheckSimStatus(); }),
        Stage([this]() -> ActionPtr<IPipeline> {
          if (modem_init_.use_pin) {
            return SetupSim(modem_init_.pin);
          }
          // ignore setup sim
          return MakeActionPtr<Pipeline>(
              action_context_, Stage<GenAction>(action_context_, []() {
                return UpdateStatus::Result();
              }));
        }),
        // save it's started
        Stage<GenAction>(action_context_, [this]() {
          started_ = true;
          SetupPoll();
          return UpdateStatus::Result();
        }));

    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{[modem_operation]() { modem_operation->Notify(); }},
        OnError{[modem_operation]() { modem_operation->Failed(); }},
        OnStop{[modem_operation]() { modem_operation->Stop(); }}});

    return pipeline;
  }));

  return modem_operation;
}

ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::Stop() {
  auto modem_operation = ActionPtr<ModemOperation>{action_context_};

  operation_queue_->Push(Stage([this, modem_operation]() {
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        // Disabling full functionality
        Stage([this]() {
          return at_comm_support_.MakeRequest("AT+CFUN=0", kWaitOk);
        }),
        Stage([this]() { return ResetModemFactory(1); }));

    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{[modem_operation]() { modem_operation->Notify(); }},
        OnError{[modem_operation]() { modem_operation->Failed(); }},
        OnStop{[modem_operation]() { modem_operation->Stop(); }}});

    return pipeline;
  }));

  return modem_operation;
}

ActionPtr<Thingy91xAtModem::OpenNetworkOperation> Thingy91xAtModem::OpenNetwork(
    Protocol protocol, std::string const& host, std::uint16_t port) {
  auto open_network_operation =
      ActionPtr<OpenNetworkOperation>{action_context_};

  operation_queue_->Push(Stage([this, open_network_operation, protocol,
                                host{host}, port]() -> ActionPtr<IPipeline> {
    if (protocol == Protocol::kTcp) {
      return OpenTcpConnection(open_network_operation, host, port);
    }
    if (protocol == Protocol::kUdp) {
      return OpenUdpConnection(open_network_operation, host, port);
    }
    return {};
  }));

  return open_network_operation;
}

ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::CloseNetwork(
    ConnectionIndex connect_index) {
  auto modem_operation = ActionPtr<ModemOperation>{action_context_};

  operation_queue_->Push(Stage([this, modem_operation, connect_index]() {
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        Stage([this, handle{static_cast<std::int32_t>(connect_index)}]() {
          // #XSOCKETSELECT=<handle>
          return at_comm_support_.MakeRequest(
              "AT#XSOCKETSELECT=" + std::to_string(handle), kWaitOk);
        }),
        Stage([this]() {
          return at_comm_support_.MakeRequest(
              "AT#XSOCKET=0", AtRequest::Wait{"OK", kTenSeconds});
        }),
        Stage<GenAction>(action_context_, [this, connect_index]() {
          connections_.erase(connect_index);
          return UpdateStatus::Result();
        }));

    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{[modem_operation]() { modem_operation->Notify(); }},
        OnError{[modem_operation]() { modem_operation->Failed(); }},
        OnStop{[modem_operation]() { modem_operation->Stop(); }}});

    return pipeline;
  }));

  return modem_operation;
}

ActionPtr<Thingy91xAtModem::WriteOperation> Thingy91xAtModem::WritePacket(
    ConnectionIndex connect_index, DataBuffer const& data) {
  if (data.size() > kModemMTU) {
    assert(false);
    return {};
  }

  auto write_notify = ActionPtr<WriteOperation>{action_context_};

  operation_queue_->Push(
      Stage([this, write_notify, connect_index, data{data}]() {
        auto write_pipeline = SendData(connect_index, data);
        write_pipeline->StatusEvent().Subscribe(ActionHandler{
            OnResult{[write_notify]() { write_notify->Notify(); }},
            OnError{[write_notify]() { write_notify->Failed(); }},
            OnStop{[write_notify]() { write_notify->Stop(); }},
        });
        return write_pipeline;
      }));

  return write_notify;
}

Thingy91xAtModem::DataEvent::Subscriber Thingy91xAtModem::data_event() {
  return EventSubscriber{data_event_};
}

ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::SetPowerSaveParam(
    ModemPowerSaveParam const& psp) {
  auto modem_operation = ActionPtr<ModemOperation>{action_context_};

  operation_queue_->Push(Stage([this, modem_operation, psp{psp}]() {
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_, Stage([this]() {
          return at_comm_support_.MakeRequest("AT+CFUN=0", kWaitOk);
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
        Stage([this]() {
          return at_comm_support_.MakeRequest("AT+CFUN=1", kWaitOk);
        }));
    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{[modem_operation]() { modem_operation->Notify(); }},
        OnError{[modem_operation]() { modem_operation->Failed(); }},
        OnStop{[modem_operation]() { modem_operation->Stop(); }}});

    return pipeline;
  }));

  return modem_operation;
}

ActionPtr<Thingy91xAtModem::ModemOperation> Thingy91xAtModem::PowerOff() {
  auto modem_operation = ActionPtr<ModemOperation>{action_context_};

  operation_queue_->Push(Stage([this, modem_operation]() {
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        // Disabling full functionality
        Stage([this]() {
          return at_comm_support_.MakeRequest("AT+CFUN=0", kWaitOk);
        }));

    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{[modem_operation]() { modem_operation->Notify(); }},
        OnError{[modem_operation]() { modem_operation->Failed(); }},
        OnStop{[modem_operation]() { modem_operation->Stop(); }}});

    return pipeline;
  }));

  return modem_operation;
}

// ============================private members=============================== //
ActionPtr<IPipeline> Thingy91xAtModem::CheckSimStatus() {
  return MakeActionPtr<Pipeline>(action_context_, Stage([this]() {
                                   return at_comm_support_.MakeRequest(
                                       "AT+CPIN?", kWaitOk);
                                 }));
}

ActionPtr<IPipeline> Thingy91xAtModem::SetupSim(std::uint16_t pin) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, pin]() -> ActionPtr<AtRequest> {
        auto pin_string = AtSupport::PinToString(pin);

        if (pin_string.empty()) {
          AE_TELED_ERROR("Pin wrong!");
          return {};
        }
        // Check SIM card status
        return at_comm_support_.MakeRequest("AT+CPIN=" + pin_string, kWaitOk);
      }));
}

ActionPtr<IPipeline> Thingy91xAtModem::SetNetMode(kModemMode const modem_mode) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, modem_mode]() -> ActionPtr<AtRequest> {
        switch (modem_mode) {
          case kModemMode::kModeAuto:
            return at_comm_support_.MakeRequest(
                "AT%XSYSTEMMODE=1,1,0,4",
                kWaitOk);  // Set modem mode Auto
          case kModemMode::kModeCatM:
            return at_comm_support_.MakeRequest(
                "AT%XSYSTEMMODE=1,0,0,1",
                kWaitOk);  // Set modem mode CatM
          case kModemMode::kModeNbIot:
            return at_comm_support_.MakeRequest(
                "AT%XSYSTEMMODE=0,1,0,2",
                kWaitOk);  // Set modem mode NbIot
          case kModemMode::kModeCatMNbIot:
            return at_comm_support_.MakeRequest(
                "AT%XSYSTEMMODE=1,1,0,0",
                kWaitOk);  // Set modem mode CatMNbIot
          default:
            return {};
        }
      }));
}

ActionPtr<IPipeline> Thingy91xAtModem::SetupNetwork(
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
        return at_comm_support_.MakeRequest(
            cmd, AtRequest::Wait{"OK", std::chrono::seconds{120}});
      }),

      Stage([this, apn_name]() {
        return at_comm_support_.MakeRequest(
            R"(AT+CGDCONT=0,"IP",")" + apn_name + "\"", kWaitOk);
      }),
      Stage([this, apn_name]() {
        return at_comm_support_.MakeRequest(
            R"(AT+CEREG=1,"IP",")" + apn_name + "\"", kWaitOk);
      }));
}

ActionPtr<IPipeline> Thingy91xAtModem::SetTxPower(
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

  return MakeActionPtr<Pipeline>(action_context_,  //
                                 Stage([this, cmd]() {
                                   return at_comm_support_.MakeRequest(
                                       cmd, AtRequest::Wait{"OK", kOneSecond});
                                 }));
}

ActionPtr<IPipeline> Thingy91xAtModem::GetTxPower(
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
ActionPtr<IPipeline> Thingy91xAtModem::SetPsm(
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
        return at_comm_support_.MakeRequest(cmd, kWaitOk);
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
ActionPtr<IPipeline> Thingy91xAtModem::SetEdrx(EdrxMode const edrx_mode,
                                               EdrxActTType const act_type,
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
      action_context_, Stage([this, cmd]() {
        return at_comm_support_.MakeRequest(cmd, kWaitOkTwoSeconds);
      }),
      Stage([this, ptw_str, act_type]() {
        std::string cmd =
            "AT%XPTW=" + std::to_string(static_cast<std::uint8_t>(act_type)) +
            ",\"" + ptw_str + "\"";
        return at_comm_support_.MakeRequest(cmd, kWaitOkTwoSeconds);
      }));
}

/**
 * Enables/disables Release Assistance Indication (RAI)
 *
 * @param mode         Mode: 0 - disable, 1 - enable, 2 - Activate with
 * unsolicited notifications
 * @return kModemError ErrorCode
 */
ActionPtr<IPipeline> Thingy91xAtModem::SetRai(std::uint8_t const rai_mode) {
  std::string cmd;
  if (rai_mode >= 3) {
    return {};
  }

  return MakeActionPtr<Pipeline>(
      action_context_,  //
      Stage([this, rai_mode]() {
        std::string cmd = "AT%RAI=" + std::to_string(rai_mode);
        return at_comm_support_.MakeRequest(cmd, kWaitOkTwoSeconds);
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
ActionPtr<IPipeline> Thingy91xAtModem::SetBandLock(
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
                                   return at_comm_support_.MakeRequest(
                                       cmd, kWaitOkTwoSeconds);
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
ActionPtr<IPipeline> Thingy91xAtModem::ResetModemFactory(
    std::uint8_t res_mode) {
  if (res_mode >= 2) {
    return {};
  }
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, res_mode]() {
        return at_comm_support_.MakeRequest(
            "AT%XFACTORYRESET=" + std::to_string(res_mode), kWaitOkTwoSeconds);
      }));
}

ActionPtr<IPipeline> Thingy91xAtModem::OpenTcpConnection(
    ActionPtr<OpenNetworkOperation> open_network_operation,
    std::string const& host, std::uint16_t port) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this, open_network_operation{std::move(open_network_operation)},
             host{host}, port]() {
        auto open_operation = ActionPtr<Thingy91TcpOpenNetwork>{
            action_context_, *this, host, port};

        open_operation->StatusEvent().Subscribe(ActionHandler{
            OnResult{[open_network_operation](auto const& action) {
              open_network_operation->SetValue(action.connection_index());
            }},
            OnError{[open_network_operation]() {
              open_network_operation->Reject();
            }},
        });

        return open_operation;
      }));
}

ActionPtr<IPipeline> Thingy91xAtModem::OpenUdpConnection(
    ActionPtr<OpenNetworkOperation> open_network_operation,
    std::string const& host, std::uint16_t port) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this, open_network_operation{std::move(open_network_operation)},
             host{host}, port]() {
        auto open_operation = ActionPtr<Thingy91UdpOpenNetwork>{
            action_context_, *this, host, port};

        open_operation->StatusEvent().Subscribe(ActionHandler{
            OnResult{[open_network_operation](auto const& action) {
              open_network_operation->SetValue(action.connection_index());
            }},
            OnError{[open_network_operation]() {
              open_network_operation->Reject();
            }},
        });

        return open_operation;
      }));
}

ActionPtr<IPipeline> Thingy91xAtModem::SendData(ConnectionIndex connection,
                                                DataBuffer const& data) {
  auto terminated_data = DataBuffer(data.size() + 3);
  std::copy(std::begin(data), std::end(data), std::begin(terminated_data));
  terminated_data[terminated_data.size() - 3] = '+';
  terminated_data[terminated_data.size() - 2] = '+';
  terminated_data[terminated_data.size() - 1] = '+';

  return MakeActionPtr<Pipeline>(
      action_context_,
      // #XSOCKETSELECT=<handle>
      Stage([this, handle{static_cast<std::int32_t>(connection)}]() {
        return at_comm_support_.MakeRequest(
            "AT#XSOCKETSELECT=" + std::to_string(handle), kWaitOk);
      }),
      // #XSEND[=<data>]
      Stage([this]() {
        return at_comm_support_.MakeRequest("AT#XSEND", kWaitOk);
      }),
      Stage([this, terminated_data{std::move(terminated_data)}]() mutable {
        return at_comm_support_.MakeRequest(
            [this, terminated_data{std::move(terminated_data)}]() {
              auto at_action = ActionPtr<AtWriteAction>{action_context_};
              serial_->Write(terminated_data);
              at_action->Notify();
              return at_action;
            },
            AtRequest::Wait{"#XDATAMODE:", kTenSeconds, [](auto&, auto pos) {
                              int code{-1};
                              AtSupport::ParseResponse(*pos, "#XDATAMODE",
                                                       code);
                              AE_TELED_DEBUG("Send data result code: {}", code);
                              return code == 0;
                            }});
      }));
}

ActionPtr<IPipeline> Thingy91xAtModem::ReadPacket(ConnectionIndex connection) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      // #XSOCKETSELECT=<handle>
      Stage([this, handle{static_cast<std::int32_t>(connection)}]() {
        return at_comm_support_.MakeRequest(
            "AT#XSOCKETSELECT=" + std::to_string(handle), kWaitOk);
      }),
      // #XRECV=<timeout>[,<flags>]
      Stage([this, connection]() {
        return at_comm_support_.MakeRequest(
            "AT#XRECV=0,64", kWaitOk,
            AtRequest::Wait{"#XRECV:", kTenSeconds,
                            [this, connection](auto& at_buffer, auto pos) {
                              std::size_t size{};
                              auto parse_end = AtSupport::ParseResponse(
                                  *pos, "#XRECV", size);
                              if (parse_end.value_or(0) == 0) {
                                AE_TELED_ERROR("Parser recv error");
                                return false;
                              }
                              auto data_crate =
                                  at_buffer.GetCrate(size, *parse_end, pos);
                              if (data_crate.size() != size) {
                                AE_TELED_ERROR("Parser recv data error");
                                return false;
                              }

                              // Emit the received data
                              data_event_.Emit(connection, data_crate);
                              return true;
                            }});
      }));
}

void Thingy91xAtModem::SetupPoll() {
  poll_listener_ =
      at_comm_support_.ListenForResponse("#XPOLL: ", [this](auto&, auto pos) {
        std::int32_t handle{};
        std::string flags;
        auto res = AtSupport::ParseResponse(*pos, "#XPOLL", handle, flags);
        if (!res) {
          AE_TELED_ERROR("Failed to parse XPOLL response");
          return;
        }
        PollEvent(handle, flags);
      });

  // TODO: config for poll interval
  poll_task_ = ActionPtr<RepeatableTask>{
      action_context_,
      [this]() {
        // add poll to operation queue
        if (connections_.empty()) {
          return;
        }
        if (poll_in_queue_ > 0) {
          return;
        }
        if (recv_in_queue_ > 0) {
          return;
        }
        operation_queue_->Push(Stage([this]() {
          poll_in_queue_++;
          auto operation = Poll();
          operation->FinishedEvent().Subscribe([this]() { poll_in_queue_--; });
          return operation;
        }));
      },
      std::chrono::milliseconds{200}};
}

ActionPtr<IPipeline> Thingy91xAtModem::Poll() {
  return MakeActionPtr<Pipeline>(action_context_,  //
                                 Stage([this]() {
                                   std::string handles;
                                   for (auto ci : connections_) {
                                     handles += "," + std::to_string(ci);
                                   }
                                   return at_comm_support_.MakeRequest(
                                       "AT#XPOLL=0" + handles, kWaitOk);
                                 }));
}

void Thingy91xAtModem::PollEvent(std::int32_t handle, std::string_view flags) {
  // flags is in hexadecimal format like "0x001"
  auto flags_val = FromChars<std::uint32_t>(flags, 16);
  if (!flags_val) {
    return;
  }

  // get connection index
  auto it = connections_.find(static_cast<ConnectionIndex>(handle));
  if (it == std::end(connections_)) {
    AE_TELED_ERROR("Poll unknown handle {}", handle);
    return;
  }

  constexpr std::uint32_t kPollIn = 0x01;
  if ((*flags_val & kPollIn) != 0) {
    recv_in_queue_++;
    operation_queue_->Push(Stage([this, connection{*it}]() {
      auto operation = ReadPacket(connection);
      operation->FinishedEvent().Subscribe([this]() { recv_in_queue_--; });
      return operation;
    }));
  }
}

} /* namespace ae */
#endif
