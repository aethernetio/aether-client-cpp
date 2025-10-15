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

#include <string_view>

#include "aether/misc/defer.h"

#include "aether/actions/pipeline.h"
#include "aether/actions/gen_action.h"
#include "aether/serial_ports/serial_port_factory.h"

#include "aether/modems/modems_tele.h"

namespace ae {
static constexpr Duration kOneSecond = std::chrono::milliseconds{1000};
static constexpr Duration kTenSeconds = std::chrono::milliseconds{10000};
static constexpr Duration kTwoMinutes = std::chrono::milliseconds{120000};

class Sim7070TcpOpenNetwork final : public Action<Sim7070TcpOpenNetwork> {
 public:
  Sim7070TcpOpenNetwork(ActionContext action_context, Sim7070AtModem& modem,
                        std::string host, std::uint16_t port)
      : Action{action_context},
        action_context_{action_context},
        modem_{&modem},
        at_comm_support_{&modem.at_comm_support_},
        host_{std::move(host)},
        port_{port},
        handle_{static_cast<std::int32_t>(modem_->next_connection_index_++)} {
    AE_TELED_DEBUG("Open TCP connection for {}:{} with handle {}", host_, port_,
                   handle_);
    operation_pipeline_ = MakeActionPtr<Pipeline>(
        action_context_,
        // AT+CAOPEN=<cid>,<pdp_index>,<conn_type>,<server>,<port>[,<recv_mode>]
        Stage([this]() {
          return at_comm_support_->SendATCommand(
              R"(AT+CAOPEN=)" + std::to_string(handle_) + R"(,0,"TCP",")" +
              host_ + "\"," + std::to_string(port_));
        }),
        Stage([this]() {
          return at_comm_support_->WaitForResponse(
              "+CAOPEN: " + std::to_string(handle_) + ",0", kTenSeconds);
        }),
        Stage<GenAction>(action_context_, [this]() {
          modem_->connections_.emplace(static_cast<ConnectionIndex>(handle_));
          return UpdateStatus::Result();
        }));

    operation_pipeline_->StatusEvent().Subscribe(ActionHandler{
        OnResult{[this]() {
          connection_index_ = static_cast<ConnectionIndex>(handle_);
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

  ConnectionIndex connection_index() const { return connection_index_; }

 private:
  ActionContext action_context_;
  Sim7070AtModem* modem_;
  AtCommSupport* at_comm_support_;
  std::string host_;
  std::uint16_t port_;
  ActionPtr<IPipeline> operation_pipeline_;
  Subscription operation_sub_;
  std::int32_t handle_;
  ConnectionIndex connection_index_ = kInvalidConnectionIndex;
  bool success_{};
  bool error_{};
  bool stop_{};
};

class Sim7070UdpOpenNetwork final : public Action<Sim7070UdpOpenNetwork> {
 public:
  Sim7070UdpOpenNetwork(ActionContext action_context, Sim7070AtModem& modem,
                        std::string host, std::uint16_t port)
      : Action{action_context},
        action_context_{action_context},
        modem_{&modem},
        at_comm_support_{&modem.at_comm_support_},
        host_{std::move(host)},
        port_{port},
        handle_{static_cast<std::int32_t>(modem_->next_connection_index_++)} {
    AE_TELED_DEBUG("Open UDP connection for {}:{} with handle {}", host_, port_,
                   handle_);
    operation_pipeline_ = MakeActionPtr<Pipeline>(
        action_context_,
        // AT+CAOPEN=<cid>,<pdp_index>,<conn_type>,<server>,<port>[,<recv_mode>]
        Stage([this]() {
          return at_comm_support_->SendATCommand(
              R"(AT+CAOPEN=)" + std::to_string(handle_) + R"(,0,"UDP",")" +
              host_ + "\"," + std::to_string(port_));
        }),
        Stage([this]() {
          return at_comm_support_->WaitForResponse(
              "+CAOPEN: " + std::to_string(handle_) + ",0", kTenSeconds);
        }),
        Stage<GenAction>(action_context_, [this]() {
          modem_->connections_.emplace(static_cast<ConnectionIndex>(handle_));
          return UpdateStatus::Result();
        }));

    operation_pipeline_->StatusEvent().Subscribe(ActionHandler{
        OnResult{[this]() {
          connection_index_ = static_cast<ConnectionIndex>(handle_);
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

  ConnectionIndex connection_index() const { return connection_index_; }

 private:
  ActionContext action_context_;
  Sim7070AtModem* modem_;
  AtCommSupport* at_comm_support_;
  std::string host_;
  std::uint16_t port_;
  ActionPtr<IPipeline> operation_pipeline_;
  Subscription operation_sub_;
  std::int32_t handle_{-1};
  ConnectionIndex connection_index_ = kInvalidConnectionIndex;
  bool success_{};
  bool error_{};
  bool stop_{};
};

Sim7070AtModem::Sim7070AtModem(ActionContext action_context,
                               IPoller::ptr const& poller, ModemInit modem_init)
    : action_context_{action_context},
      modem_init_{std::move(modem_init)},
      serial_{SerialPortFactory::CreatePort(action_context_, poller,
                                            modem_init_.serial_init)},
      at_comm_support_{action_context_, *serial_},
      operation_queue_{action_context_},
      initiated_{false},
      started_{false} {
  AE_TELED_DEBUG("Sim7070AtModem init");
  Init();
}

void Sim7070AtModem::Init() {
  operation_queue_->Push(Stage([this]() {
    auto init_pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        Stage([this]() { return at_comm_support_.SendATCommand("AT"); }),
        Stage([this]() {
          return at_comm_support_.WaitForResponse("OK", kOneSecond);
        }),
        Stage([this]() { return at_comm_support_.SendATCommand("ATE0"); }),
        Stage([this]() {
          return at_comm_support_.WaitForResponse("OK", kOneSecond);
        }),
        Stage([this]() { return at_comm_support_.SendATCommand("AT+CMEE=1"); }),
        Stage([this]() {
          return at_comm_support_.WaitForResponse("OK", kOneSecond);
        }),
        Stage<GenAction>(action_context_, [this]() {
          initiated_ = true;
          return UpdateStatus::Result();
        }));

    init_pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{[]() { AE_TELED_INFO("Sim7070AtModem init success"); }},
        OnError{[]() { AE_TELED_ERROR("Sim7070AtModem init failed"); }},
    });

    return init_pipeline;
  }));
}

ActionPtr<Sim7070AtModem::ModemOperation> Sim7070AtModem::Start() {
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
        // Enabling full functionality
        Stage(
            [this]() { return at_comm_support_.SendATCommand("AT+CFUN=1,0"); }),
        Stage([this]() {
          return at_comm_support_.WaitForResponse("OK", kOneSecond);
        }),
        Stage([this]() {
          return SetBaudRate(modem_init_.serial_init.baud_rate);
        }),
        Stage([this]() { return SetNetMode(modem_init_.modem_mode); }),
        Stage([this]() {
          return SetupNetwork(modem_init_.operator_name,
                              modem_init_.operator_code, modem_init_.apn_name,
                              modem_init_.apn_user, modem_init_.apn_pass,
                              modem_init_.modem_mode, modem_init_.auth_type);
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

ActionPtr<Sim7070AtModem::ModemOperation> Sim7070AtModem::Stop() {
  auto modem_operation = ActionPtr<ModemOperation>{action_context_};

  operation_queue_->Push(Stage([this, modem_operation]() {
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        // AT+CNACT=<pdpidx>,<action> // Deactivate the PDP context
        Stage([this]() {
          return at_comm_support_.SendATCommand("AT+CNACT=0,0");
        }),
        Stage([this]() {
          return at_comm_support_.WaitForResponse("+APP PDP: 0,DEACTIVE",
                                                  kTenSeconds);
        }),
        // Reset modem settings correctly
        Stage([this]() { return at_comm_support_.SendATCommand("ATZ"); }),
        Stage([this]() {
          return at_comm_support_.WaitForResponse("OK", kOneSecond);
        }),
        // Disabling full functionality
        Stage([this]() { return at_comm_support_.SendATCommand("AT+CFUN=0"); }),
        Stage([this]() {
          return at_comm_support_.WaitForResponse("OK", kOneSecond);
        }),
        Stage<GenAction>(action_context_, [this]() {
          started_ = false;
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

ActionPtr<Sim7070AtModem::OpenNetworkOperation> Sim7070AtModem::OpenNetwork(
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

ActionPtr<Sim7070AtModem::ModemOperation> Sim7070AtModem::CloseNetwork(
    ConnectionIndex connect_index) {
  auto modem_operation = ActionPtr<ModemOperation>{action_context_};

  operation_queue_->Push(Stage([this, modem_operation, connect_index]() {
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        // AT+CACLOSE=<cid> // Close TCP/UDP socket
        Stage([this, connect_index]() {
          return at_comm_support_.SendATCommand("AT+CACLOSE=" +
                                                std::to_string(connect_index));
        }),
        Stage([this]() {
          return at_comm_support_.WaitForResponse("OK", kOneSecond);
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

ActionPtr<Sim7070AtModem::WriteOperation> Sim7070AtModem::WritePacket(
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

Sim7070AtModem::DataEvent::Subscriber Sim7070AtModem::data_event() {
  return EventSubscriber{data_event_};
}

ActionPtr<Sim7070AtModem::ModemOperation> Sim7070AtModem::SetPowerSaveParam(
    PowerSaveParam const& psp) {
  auto modem_operation = ActionPtr<ModemOperation>{action_context_};

  operation_queue_->Push(Stage([this, modem_operation, psp{psp}]() {
    // TODO: Implement SIM7070 specific power save parameters
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_, Stage<GenAction>(action_context_, []() {
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

ActionPtr<Sim7070AtModem::ModemOperation> Sim7070AtModem::PowerOff() {
  auto modem_operation = ActionPtr<ModemOperation>{action_context_};

  operation_queue_->Push(Stage([this, modem_operation]() {
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        // Disabling full functionality
        Stage(
            [this]() { return at_comm_support_.SendATCommand("AT+CPOWD=1"); }),
        Stage([this]() {
          return at_comm_support_.WaitForResponse("OK", kOneSecond);
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
ActionPtr<IPipeline> Sim7070AtModem::CheckSimStatus() {
  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this]() { return at_comm_support_.SendATCommand("AT+CPIN?"); }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse("OK", kOneSecond);
      }));
}

ActionPtr<IPipeline> Sim7070AtModem::SetupSim(std::uint16_t pin) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this, pin]() -> ActionPtr<AtCommSupport::WriteAction> {
        auto pin_string = AtCommSupport::PinToString(pin);

        if (pin_string.empty()) {
          AE_TELED_ERROR("Pin wrong!");
          return {};
        }
        // Check SIM card status
        return at_comm_support_.SendATCommand("AT+CPIN=" + pin_string);
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse("OK", kOneSecond);
      }));
}

ActionPtr<IPipeline> Sim7070AtModem::SetBaudRate(kBaudRate const rate) {
  static const std::map<kBaudRate, std::string_view>
      baud_rate_commands_sim7070 = {
          {kBaudRate::kBaudRate0, "AT+IPR=0"},
          {kBaudRate::kBaudRate300, "AT+IPR=300"},
          {kBaudRate::kBaudRate600, "AT+IPR=600"},
          {kBaudRate::kBaudRate1200, "AT+IPR=1200"},
          {kBaudRate::kBaudRate2400, "AT+IPR=2400"},
          {kBaudRate::kBaudRate4800, "AT+IPR=4800"},
          {kBaudRate::kBaudRate9600, "AT+IPR=9600"},
          {kBaudRate::kBaudRate19200, "AT+IPR=19200"},
          {kBaudRate::kBaudRate38400, "AT+IPR=38400"},
          {kBaudRate::kBaudRate57600, "AT+IPR=57600"},
          {kBaudRate::kBaudRate115200, "AT+IPR=115200"},
          {kBaudRate::kBaudRate230400, "AT+IPR=230400"},
          {kBaudRate::kBaudRate921600, "AT+IPR=921600"},
          {kBaudRate::kBaudRate2000000, "AT+IPR=2000000"},
          {kBaudRate::kBaudRate2900000, "AT+IPR=2900000"},
          {kBaudRate::kBaudRate3000000, "AT+IPR=3000000"},
          {kBaudRate::kBaudRate3200000, "AT+IPR=3200000"},
          {kBaudRate::kBaudRate3684000, "AT+IPR=3684000"},
          {kBaudRate::kBaudRate4000000, "AT+IPR=4000000"}};

  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this, rate]() -> ActionPtr<AtCommSupport::WriteAction> {
        auto it = baud_rate_commands_sim7070.find(rate);
        if (it == baud_rate_commands_sim7070.end()) {
          return {};
        }
        return at_comm_support_.SendATCommand(std::string{it->second});
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse("OK", kOneSecond);
      }));
}

ActionPtr<IPipeline> Sim7070AtModem::SetNetMode(kModemMode const modem_mode) {
  static const std::map<kModemMode, std::string_view>
      net_mode_commands_sim7070 = {
          {kModemMode::kModeAuto, "AT+CNMP=2"},      // Set modem mode Auto
          {kModemMode::kModeGSMOnly, "AT+CNMP=13"},  // Set modem mode GSMOnly
          {kModemMode::kModeLTEOnly, "AT+CNMP=38"},  // Set modem mode LTEOnly
          {kModemMode::kModeGSMLTE, "AT+CNMP=51"},   // Set modem mode GSMLTE
          {kModemMode::kModeCatM, "AT+CMNB=1"},      // Set modem mode CatM
          {kModemMode::kModeNbIot, "AT+CMNB=2"},     // Set modem mode NbIot
          {kModemMode::kModeCatMNbIot,
           "AT+CMNB=3"},  // Set modem mode CatMNbIot
      };

  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this, modem_mode]() -> ActionPtr<AtCommSupport::WriteAction> {
        auto it = net_mode_commands_sim7070.find(modem_mode);
        if (it == net_mode_commands_sim7070.end()) {
          return {};
        }
        return at_comm_support_.SendATCommand(std::string{it->second});
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse("OK", kOneSecond);
      }));
}

ActionPtr<IPipeline> Sim7070AtModem::SetupNetwork(
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

  return MakeActionPtr<Pipeline>(
      action_context_,
      // Connect to the network
      Stage([this, operator_name, mode, operator_code]() {
        std::string cmd;
        if (!operator_name.empty()) {
          // Operator long name
          cmd = "AT+COPS=1,0,\"" + operator_name + "\"," + mode;
        } else if (!operator_code.empty()) {
          // Operator code
          cmd = "AT+COPS=1,2,\"" + operator_code + "\"," + mode;
        } else {
          // Auto
          cmd = "AT+COPS=0";
        }
        return at_comm_support_.SendATCommand(cmd);
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse("OK", kTwoMinutes);
      }),
      Stage([this, apn_name]() {
        return at_comm_support_.SendATCommand(R"(AT+CGDCONT=1,"IP",")" +
                                              apn_name + "\"");
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse("OK", kOneSecond);
      }),
      // AT+CNCFG=<pdpidx>,<ip_type>,[<APN>,[<usename>,<password>,[<authentication>]]]
      Stage([this, apn_name, apn_user, apn_pass, type]() {
        return at_comm_support_.SendATCommand("AT+CNCFG=0,0,\"" + apn_name +
                                              "\",\"" + apn_user + "\",\"" +
                                              apn_pass + "\"," + type);
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse("OK", kOneSecond);
      }),
      Stage([this]() {
        return at_comm_support_.SendATCommand("AT+CREG=1;+CGREG=1;+CEREG=1");
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse("OK", kOneSecond);
      }),
      // AT+CNACT=<pdpidx>,<action> // Activate the PDP context
      Stage(
          [this]() { return at_comm_support_.SendATCommand("AT+CNACT=0,1"); }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse("OK", kOneSecond);
      }));
}

ActionPtr<IPipeline> Sim7070AtModem::SetupProtoPar() {
  // TODO: Implement protocol parameters setup
  return MakeActionPtr<Pipeline>(action_context_,
                                 Stage<GenAction>(action_context_, []() {
                                   return UpdateStatus::Result();
                                 }));
}

ActionPtr<IPipeline> Sim7070AtModem::OpenTcpConnection(
    ActionPtr<OpenNetworkOperation> open_network_operation,
    std::string const& host, std::uint16_t port) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this, open_network_operation{std::move(open_network_operation)},
             host{host}, port]() {
        auto open_operation = ActionPtr<Sim7070TcpOpenNetwork>{
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

ActionPtr<IPipeline> Sim7070AtModem::OpenUdpConnection(
    ActionPtr<OpenNetworkOperation> open_network_operation,
    std::string const& host, std::uint16_t port) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this, open_network_operation{std::move(open_network_operation)},
             host{host}, port]() {
        auto open_operation = ActionPtr<Sim7070UdpOpenNetwork>{
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

ActionPtr<IPipeline> Sim7070AtModem::SendData(ConnectionIndex connection,
                                              DataBuffer const& data) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      // AT+CASEND=<cid>,<datalen>[,<inputtime>]
      Stage([this, connection, data]() {
        return at_comm_support_.SendATCommand(
            "AT+CASEND=" + std::to_string(connection) + "," +
            std::to_string(data.size()));
      }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse(">", kOneSecond);
      }),
      Stage<GenAction>(action_context_,
                       [this, data]() {
                         serial_->Write(data);
                         return UpdateStatus::Result();
                       }),
      Stage([this]() {
        return at_comm_support_.WaitForResponse("OK", kOneSecond);
      }));
}

ActionPtr<IPipeline> Sim7070AtModem::ReadPacket(ConnectionIndex connection) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      // AT+CARECV=<cid>,<readlen>
      Stage([this, connection]() {
        return at_comm_support_.SendATCommand(
            "AT+CARECV=" + std::to_string(connection) + ",1024");
      }),
      Stage([this, connection]() {
        return at_comm_support_.WaitForResponse(
            "+CARECV: ", kOneSecond,
            [this, connection](auto& at_buffer, auto pos) {
              // remove used pos from buffer
              defer[&]() { at_buffer.erase(pos); };

              // Parse the response format: +CARECV: [<remote IP>,<remote
              // port>,]<recvlen>,...data
              std::ptrdiff_t size{};

              if (!AtCommSupport::ParseResponse(*pos, "+CARECV", size)) {
                AE_TELED_ERROR("Failed to parse CARECV response");
                return UpdateStatus::Error();
              }

              AE_TELED_DEBUG("Received {} bytes for connection {}", size,
                             connection);

              // Extract the actual data
              std::string_view response_string(
                  reinterpret_cast<char const*>(pos->data()), pos->size());

              // Find the start of the data after the size field
              auto data_start = response_string.find(',');
              if (data_start == std::string_view::npos) {
                AE_TELED_ERROR("CARECV response missing data separator");
                return UpdateStatus::Error();
              }
              data_start += 1;  // Skip the comma

              if (static_cast<std::ptrdiff_t>(data_start) + size >
                  pos->size()) {
                AE_TELED_ERROR(
                    "Received data size mismatch: expected {}, available {}",
                    size, pos->size() - data_start);
                return UpdateStatus::Error();
              }

              DataBuffer recv_data(
                  pos->begin() + static_cast<std::ptrdiff_t>(data_start),
                  pos->begin() + static_cast<std::ptrdiff_t>(data_start) +
                      size);

              // Emit the received data
              data_event_.Emit(connection, recv_data);
              return UpdateStatus::Result();
            });
      }));
}

void Sim7070AtModem::SetupPoll() {
  poll_listener_ = at_comm_support_.ListenForResponse(
      "+CADATAIND: ", [this](auto& at_buffer, auto pos) {
        std::int32_t cid{};
        AtCommSupport::ParseResponse(*pos, "+CADATAIND", cid);
        PollEvent(cid);
        return at_buffer.erase(pos);
      });
}

void Sim7070AtModem::PollEvent(std::int32_t handle) {
  // get connection index
  auto it = connections_.find(static_cast<ConnectionIndex>(handle));
  if (it == std::end(connections_)) {
    AE_TELED_ERROR("Poll unknown handle {}", handle);
    return;
  }

  AE_TELED_DEBUG("Data available for connection {}", handle);
  operation_queue_->Push(
      Stage([this, connection{*it}]() { return ReadPacket(connection); }));
}

} /* namespace ae */
