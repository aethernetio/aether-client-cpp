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
#if AE_SUPPORT_MODEMS && AE_ENABLE_SIM7070

#  include <string_view>

#  include "aether/misc/defer.h"

#  include "aether/actions/pipeline.h"
#  include "aether/actions/gen_action.h"
#  include "aether/serial_ports/serial_port_factory.h"

#  include "aether/modems/modems_tele.h"

namespace ae {
static constexpr Duration kOneSecond = std::chrono::milliseconds{1000};
static constexpr Duration kTenSeconds = std::chrono::milliseconds{10000};
static constexpr Duration kTwoMinutes = std::chrono::milliseconds{120000};
static const AtRequest::Wait kWaitOk{"OK", kOneSecond};
static const AtRequest::Wait kWaitOkTenSeconds{"OK", kTenSeconds};

Sim7070AtModem::Sim7070AtModem(ActionContext action_context,
                               IPoller::ptr const& poller, ModemInit modem_init)
    : action_context_{action_context},
      modem_init_{std::move(modem_init)},
      serial_{SerialPortFactory::CreatePort(action_context_, poller,
                                            modem_init_.serial_init)},
      at_support_{action_context_, *serial_},
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
        Stage([this]() { return at_support_.MakeRequest("AT", kWaitOk); }),
        Stage([this]() { return at_support_.MakeRequest("ATE0", kWaitOk); }),
        Stage(
            [this]() { return at_support_.MakeRequest("AT+CMEE=1", kWaitOk); }),
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
        Stage([this]() {
          return at_support_.MakeRequest("AT+CFUN=1,0", kWaitOk);
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
          return at_support_.MakeRequest("AT+CNACT=0,0", kWaitOk);
        }),
        Stage([this]() {
          return at_support_.MakeRequest(
              "AT", AtRequest::Wait{"+APP PDP: 0,DEACTIVE", kTenSeconds});
        }),
        // Reset modem settings correctly
        Stage([this]() { return at_support_.MakeRequest("ATZ", kWaitOk); }),
        // Disabling full functionality
        Stage(
            [this]() { return at_support_.MakeRequest("AT+CFUN=0", kWaitOk); }),
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
    return OpenConnection(open_network_operation, protocol, host, port);
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
          return at_support_.MakeRequest(
              "AT+CACLOSE=" + std::to_string(connect_index), kWaitOk);
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

  AE_TELED_DEBUG("Queue write packet for data size {}", data.size());

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
    auto pipeline = MakeActionPtr<Pipeline>(action_context_,
                                            // Disabling full functionality
                                            Stage([this]() {
                                              return at_support_.MakeRequest(
                                                  "AT+CPOWD=1", kWaitOk);
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
      Stage([this]() { return at_support_.MakeRequest("AT+CPIN?", kWaitOk); }));
}

ActionPtr<IPipeline> Sim7070AtModem::SetupSim(std::uint16_t pin) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, pin]() -> ActionPtr<AtRequest> {
        auto pin_string = AtSupport::PinToString(pin);

        if (pin_string.empty()) {
          AE_TELED_ERROR("Pin wrong!");
          return {};
        }
        // Check SIM card status
        return at_support_.MakeRequest("AT+CPIN=" + pin_string, kWaitOk);
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
      action_context_, Stage([this, rate]() -> ActionPtr<AtRequest> {
        auto it = baud_rate_commands_sim7070.find(rate);
        if (it == baud_rate_commands_sim7070.end()) {
          return {};
        }
        return at_support_.MakeRequest(std::string{it->second}, kWaitOk);
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
      action_context_, Stage([this, modem_mode]() -> ActionPtr<AtRequest> {
        auto it = net_mode_commands_sim7070.find(modem_mode);
        if (it == net_mode_commands_sim7070.end()) {
          return {};
        }
        return at_support_.MakeRequest(std::string{it->second}, kWaitOk);
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
        return at_support_.MakeRequest(cmd, AtRequest::Wait{"OK", kTwoMinutes});
      }),
      Stage([this, apn_name]() {
        return at_support_.MakeRequest(
            R"(AT+CGDCONT=1,"IP",")" + apn_name + "\"", kWaitOk);
      }),
      // AT+CNCFG=<pdpidx>,<ip_type>,[<APN>,[<usename>,<password>,[<authentication>]]]
      Stage([this, apn_name, apn_user, apn_pass, type]() {
        return at_support_.MakeRequest("AT+CNCFG=0,0,\"" + apn_name + "\",\"" +
                                           apn_user + "\",\"" + apn_pass +
                                           "\"," + type,
                                       kWaitOk);
      }),
      Stage([this]() {
        return at_support_.MakeRequest("AT+CREG=1;+CGREG=1;+CEREG=1", kWaitOk);
      }),
      // AT+CNACT=<pdpidx>,<action> // Activate the PDP context
      Stage([this]() {
        return at_support_.MakeRequest("AT+CNACT=0,1", kWaitOk);
      }));
}

ActionPtr<IPipeline> Sim7070AtModem::SetupProtoPar() {
  // TODO: Implement protocol parameters setup
  return MakeActionPtr<Pipeline>(action_context_,
                                 Stage<GenAction>(action_context_, []() {
                                   return UpdateStatus::Result();
                                 }));
}

ActionPtr<IPipeline> Sim7070AtModem::OpenConnection(
    ActionPtr<OpenNetworkOperation> const& open_network_operation,
    Protocol protocol, std::string const& host, std::uint16_t port) {
  auto handle = next_connection_index_++;
  auto open_operation = MakeActionPtr<Pipeline>(
      action_context_,
      // AT+CAOPEN=<cid>,<pdp_index>,<conn_type>,<server>,<port>[,<recv_mode>]
      Stage([this, handle, protocol, host, port]() {
        auto protocol_str = std::invoke([&]() -> std::string_view {
          if (protocol == Protocol::kTcp) {
            return "TCP";
          }
          if (protocol == Protocol::kUdp) {
            return "UDP";
          }
          return "UNKNOWN";
        });
        return at_support_.MakeRequest(
            Format(R"(AT+CAOPEN={},0,"{}","{}",{})",
                   static_cast<std::int32_t>(handle), protocol_str, host, port),
            AtRequest::Wait{
                Format("+CAOPEN: {},0", static_cast<std::int32_t>(handle)),
                kTenSeconds});
      }),
      Stage<GenAction>(action_context_, [this, handle]() {
        connections_.emplace(static_cast<ConnectionIndex>(handle));
        return UpdateStatus::Result();
      }));

  open_operation->StatusEvent().Subscribe(ActionHandler{
      OnResult{[open_network_operation, handle]() {
        open_network_operation->SetValue(handle);
      }},
      OnError{[open_network_operation]() { open_network_operation->Reject(); }},
  });

  return open_operation;
}

ActionPtr<IPipeline> Sim7070AtModem::SendData(ConnectionIndex connection,
                                              DataBuffer const& data) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      // AT+CASEND=<cid>,<datalen>[,<inputtime>]
      Stage([this, connection, size{data.size()}]() {
        return at_support_.MakeRequest(
            "AT+CASEND=" + std::to_string(connection) + "," +
                std::to_string(size),
            AtRequest::Wait{">", kOneSecond});
      }),
      Stage([this, data{data}]() mutable {
        return at_support_.MakeRequest(
            [this, data{std::move(data)}]() {
              auto write_action = ActionPtr<AtWriteAction>{action_context_};
              serial_->Write(data);
              write_action->Notify();
              return write_action;
            },
            kWaitOk);
      }));
}

ActionPtr<IPipeline> Sim7070AtModem::ReadPacket(ConnectionIndex connection) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      // AT+CARECV=<cid>,<readlen>
      Stage([this, connection]() {
        return at_support_.MakeRequest(
            "AT+CARECV=" + std::to_string(connection) + ",1024", kWaitOk,
            AtRequest::Wait{
                "+CARECV: ", kOneSecond,
                [this, connection](auto& at_buffer, auto pos) {
                  // Parse the response format: +CARECV: [<remote IP>,<remote
                  // port>,]<recvlen>,...data
                  std::size_t size{};

                  auto parse_res =
                      AtSupport::ParseResponse(*pos, "+CARECV", size);
                  if (!parse_res) {
                    AE_TELED_ERROR("Failed to parse CARECV response");
                    return false;
                  }
                  AE_TELED_DEBUG("Received data size {} connection {}", size,
                                 static_cast<std::int32_t>(connection));

                  // Extract the actual data
                  auto recv_data =
                      at_buffer.GetCrate(size, *parse_res + 1, pos);
                  AE_TELED_DEBUG("Received size {} bytes \ndata{}",
                                 recv_data.size(), recv_data);

                  if (recv_data.size() != size) {
                    AE_TELED_ERROR("Received {} bytes, expected {}",
                                   recv_data.size(), size);
                    return false;
                  }

                  // Emit the received data
                  data_event_.Emit(connection, recv_data);
                  return true;
                }});
      }));
}

void Sim7070AtModem::SetupPoll() {
  poll_listener_ =
      at_support_.ListenForResponse("+CADATAIND: ", [this](auto&, auto pos) {
        std::int32_t cid{};
        AtSupport::ParseResponse(*pos, "+CADATAIND", cid);
        PollEvent(cid);
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
#endif
