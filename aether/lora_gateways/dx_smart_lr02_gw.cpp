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

#include "aether/lora_gateways/dx_smart_lr02_gw.h"

#include <bitset>
#include <string_view>

#include "aether/misc/defer.h"
#include "aether/misc/from_chars.h"
#include "aether/actions/pipeline.h"
#include "aether/actions/gen_action.h"
#include "aether/mstream_buffers.h"
#include "aether/serial_ports/serial_port_factory.h"

#include "aether/lora_modules/lora_modules_tele.h"

namespace ae {
static constexpr Duration kOneSecond = std::chrono::milliseconds{1000};
static constexpr Duration kTwoSeconds = std::chrono::milliseconds{2000};
static constexpr Duration kTenSeconds = std::chrono::milliseconds{10000};
static const AtRequest::Wait kWaitOk{"OK", kOneSecond};
static const AtRequest::Wait kWaitOkTwoSeconds{"OK", kTwoSeconds};
static const AtRequest::Wait kWaitEntryAt{"Entry AT", kOneSecond};
static const AtRequest::Wait kWaitExitAt{"Exit AT", kOneSecond};
static const AtRequest::Wait kWaitPowerOn{"Power on", kOneSecond};

class DxSmartLr02TcpOpenNetwork final
    : public Action<DxSmartLr02TcpOpenNetwork> {
 public:
  DxSmartLr02TcpOpenNetwork(ActionContext action_context,
                            DxSmartLr02LoraModule& lora_module,
                            std::string host, std::uint16_t port)
      : Action{action_context},
        action_context_{action_context},
        lora_module_{&lora_module},
        at_comm_support_{&lora_module.at_comm_support_},
        host_{std::move(host)},
        port_{port} {
    AE_TELED_DEBUG("Open tcp connection for {}:{}", host_, port_);
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

  ConnectionLoraGatewayIndex connection_index() const {
    return connection_index_;
  }

 private:
  ActionContext action_context_;
  DxSmartLr02LoraGateway* lora_gateway_;
  AtSupport* at_comm_support_;
  std::string host_;
  std::uint16_t port_;
  ActionPtr<IPipeline> operation_pipeline_;
  Subscription operation_sub_;
  std::int32_t handle_{-1};
  ConnectionLoraGatewayIndex connection_index_ =
      kInvalidConnectionLoraGatewayIndex;
  bool success_{};
  bool error_{};
  bool stop_{};
};

class DxSmartLr02UdpOpenNetwork final
    : public Action<DxSmartLr02UdpOpenNetwork> {
 public:
  DxSmartLr02UdpOpenNetwork(ActionContext action_context,
                            DxSmartLr02LoraGateway& lora_gateway,
                            std::string host, std::uint16_t port)
      : Action{action_context},
        action_context_{action_context},
        lora_gateway_{&lora_gateway},
        at_comm_support_{&lora_gateway.at_comm_support_},
        host_{std::move(host)},
        port_{port} {
    AE_TELED_DEBUG("Open UDP connection for {}:{}", host_, port_);
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

  ConnectionLoraGatewayIndex connection_index() const { return connection_index_; }

 private:
  ActionContext action_context_;
  DxSmartLr02LoraGateway* lora_gateway_;
  AtSupport* at_comm_support_;
  std::string host_;
  std::uint16_t port_;
  ActionPtr<IPipeline> operation_pipeline_;
  Subscription operation_sub_;
  std::int32_t handle_{-1};
  ConnectionLoraGatewayIndex connection_index_ =
      kInvalidConnectionLoraModuleIndex;
  bool success_{};
  bool error_{};
  bool stop_{};
};

DxSmartLr02LoraGateway::DxSmartLr02LoraGateway(ActionContext action_context,
                                             IPoller::ptr const& poller,
                                             LoraGatewayInit lora_gateway_init)
    : action_context_{action_context},
      lora_gateway_init_{std::move(lora_gateway_init)},
      serial_{SerialPortFactory::CreatePort(action_context_, poller,
                                            lora_gateway_init_.serial_init)},
      at_comm_support_{action_context_, *serial_},
      operation_queue_{action_context_},
      initiated_{false},
      started_{false} {
  Init();
}

DxSmartLr02LoraGateway::~DxSmartLr02LoraGateway() { Stop(); }

ActionPtr<DxSmartLr02LoraGateway::LoraGatewayOperation>
DxSmartLr02LoraGateway::Start() {
  auto lora_gateway_operation = ActionPtr<LoraGatewayOperation>{action_context_};
  operation_queue_->Push(Stage([this, lora_module_operation]()
                                   -> ActionPtr<IPipeline> {
    // if already started, notify of success and return
    if (started_) {
      return MakeActionPtr<Pipeline>(
          action_context_,
          Stage<GenAction>(action_context_, [lora_gateway_operation]() {
            lora_gateway_operation->Notify();
            return UpdateStatus::Result();
          }));
    }

    auto pipeline =
        MakeActionPtr<Pipeline>(action_context_,
                                // Enter AT command mode
                                Stage([this]() { return EnterAtMode(); }),
                                // Exit AT command mode
                                Stage([this]() { return ExitAtMode(); }),
                                // save it's started
                                Stage<GenAction>(action_context_, [this]() {
                                  started_ = true;
                                  SetupPoll();
                                  return UpdateStatus::Result();
                                }));

    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{
            [lora_gateway_operation]() { lora_gateway_operation->Notify(); }},
        OnError{[lora_module_operation]() { lora_gateway_operation->Failed(); }},
        OnStop{[lora_module_operation]() { lora_gateway_operation->Stop(); }}});

    return pipeline;
  }));

  return lora_gateway_operation;
}

ActionPtr<DxSmartLr02LoraGateway::LoraGatewayOperation>
DxSmartLr02LoraGateway::Stop() {
  auto lora_gateway_operation = ActionPtr<LoraGatewayOperation>{action_context_};

  operation_queue_->Push(Stage([this, lora_gateway_operation]() {
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        // Enter AT command mode
        Stage([this]() { return EnterAtMode(); }), Stage([this]() {
          return at_comm_support_.MakeRequest("AT+RESET", kWaitOk);
        }),
        // Exit AT command mode
        Stage([this]() { return ExitAtMode(); }));

    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{
            [lora_gateway_operation]() { lora_gateway_operation->Notify(); }},
        OnError{[lora_gatewaye_operation]() { lora_gateway_operation->Failed(); }},
        OnStop{[lora_gateway_operation]() { lora_gateway_operation->Stop(); }}});

    return pipeline;
  }));

  return lora_gateway_operation;
}

ActionPtr<DxSmartLr02LoraGateway::OpenNetworkOperation>
DxSmartLr02LoraGateway::OpenNetwork(ae::Protocol protocol,
                                   std::string const& host,
                                   std::uint16_t port) {
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

ActionPtr<DxSmartLr02LoraModule::LoraGatewayOperation>
DxSmartLr02LoraGateway::CloseNetwork(
    ae::ConnectionLoraGatewayIndex /*connect_index*/) {
  return {};
}
// void DxSmartLr02LoraModule::CloseNetwork(
//    ae::ConnectionLoraIndex connect_index) {
//  if (connect_index >= connect_vec_.size()) {
//    AE_TELED_ERROR("Connection index overflow");
//    return;
//  }
//
//  connect_vec_.erase(connect_vec_.begin() + connect_index);
//};

ActionPtr<DxSmartLr02LoraGateway::WriteOperation>
DxSmartLr02LoraGateway::WritePacket(
    ae::ConnectionLoraGatewayIndex /*connect_index*/,
                                   ae::DataBuffer const& /*data*/) {
  return {};
}
// void DxSmartLr02LoraModule::WritePacket(
//    ae::ConnectionLoraIndex connect_index,
//                                        ae::DataBuffer const& data) {
//  LoraPacket lora_packet{};
//
//  auto const& connection =
//      connect_vec_.at(static_cast<std::size_t>(connect_index));
//
//  lora_packet.connection = connection;
//  lora_packet.length = data.size();
//  lora_packet.data = data;
//  lora_packet.crc = 0;  // Not implemented yet
//
//  auto packet_data = std::vector<std::uint8_t>{};
//  VectorWriter<PacketSize> vw{packet_data};
//  auto os = omstream{vw};
//  // copy data with size
//  os << lora_packet;
//
//  serial_->Write(packet_data);
//};

// DataBuffer DxSmartLr02LoraModule::ReadPacket(
//     ae::ConnectionLoraIndex /* connect_index*/, ae::Duration /* timeout*/) {
//   LoraPacket lora_packet{};
//   DataBuffer data{};
//
//   auto response = serial_->Read();
//   std::vector<std::uint8_t> packet_data(response->begin(), response->end());
//   VectorReader<PacketSize> vr(packet_data);
//   auto is = imstream{vr};
//   // copy data with size
//   is >> lora_packet;
//
//   data = lora_packet.data;
//
//   return data;
// };

ActionPtr<IPipeline> DxSmartLr02LoraGateway::ReadPacket(
    ConnectionLoraGatewayIndex /* connection */) {
  return {};
}

DxSmartLr02LoraGateway::DataEvent::Subscriber
DxSmartLr02LoraGateway::data_event() {
  return EventSubscriber{data_event_};
}

ActionPtr<DxSmartLr02LoraGateway::LoraGatewayOperation>
DxSmartLr02LoraGateway::SetPowerSaveParam(LoraGatewayPowerSaveParam const& psp) {
  auto lora_gateway_operation = ActionPtr<LoraGatewayOperation>{action_context_};

  operation_queue_->Push(Stage([this, lora_gateway_operation, psp{psp}]() {
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        // Enter AT command mode
        Stage([this]() { return EnterAtMode(); }),
        // Configure Lora Gateway Mode
        Stage(
            [this, psp]() { return SetLoraGatewayMode(psp.lora_module_mode); }),
        // Configure Lora Gateway Level
        Stage([this, psp]() {
          return SetLoraGatewayLevel(psp.lora_module_level);
        }),
        // Configure Lora Gateway Power
        Stage([this, psp]() {
          return SetLoraGatewayPower(psp.lora_module_power);
        }),
        // Configure Lora Gateway BandWidth
        Stage([this, psp]() {
          return SetLoraGatewayBandWidth(psp.lora_module_band_width);
        }),
        // Configure Lora Gateway Coding Rate
        Stage([this, psp]() {
          return SetLoraGatewayCodingRate(psp.lora_module_coding_rate);
        }),
        // Configure Lora Gateway Spreading Factor
        Stage([this, psp]() {
          return SetLoraGatewaySpreadingFactor(psp.lora_module_spreading_factor);
        }),
        // Exit AT command mode
        Stage([this]() { return ExitAtMode(); }));
    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{
            [lora_gateway_operation]() { lora_gateway_operation->Notify(); }},
        OnError{[lora_gateway_operation]() { lora_gateway_operation->Failed(); }},
        OnStop{[lora_gateway_operation]() { lora_gateway_operation->Stop(); }}});

    return pipeline;
  }));

  return lora_gateway_operation;
}

ActionPtr<DxSmartLr02LoraGateway::LoraGatewayOperation>
DxSmartLr02LoraGateway::PowerOff() {
  return {};
}

ActionPtr<DxSmartLr02LoraGateway::LoraGatewayOperation>
DxSmartLr02LoraGateway::SetLoraGatewayAddress(std::uint16_t const& address) {
  auto lora_gateway_operation = ActionPtr<LoraGatewayOperation>{action_context_};

  operation_queue_->Push(Stage([this, lora_gateway_operation, address]() {
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        // Enter AT command mode
        Stage([this]() { return EnterAtMode(); }), Stage([this, address]() {
          return at_comm_support_.MakeRequest(
              "AT+MAC" + AdressToString(address), kWaitOk);
        }),
        // Exit AT command mode
        Stage([this]() { return ExitAtMode(); }));

    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{
            [lora_gateway_operation]() { lora_gateway_operation->Notify(); }},
        OnError{[lora_gateway_operation]() { lora_gateway_operation->Failed(); }},
        OnStop{[lora_gateway_operation]() { lora_gateway_operation->Stop(); }}});

    return pipeline;
  }));

  return lora_gateway_operation;
}

ActionPtr<DxSmartLr02LoraGateway::LoraGatewayOperation>
DxSmartLr02LoraGateway::SetLoraGatewayChannel(std::uint8_t const& channel) {
  if (channel > 0x1E) {
    return {};
  }

  auto lora_gateway_operation = ActionPtr<LoraGatewayOperation>{action_context_};

  operation_queue_->Push(Stage([this, lora_gateway_operation, channel]() {
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        // Enter AT command mode
        Stage([this]() { return EnterAtMode(); }), Stage([this, channel]() {
          return at_comm_support_.MakeRequest(
              "AT+CHANNEL" + ChannelToString(channel), kWaitOk);
        }),
        // Exit AT command mode
        Stage([this]() { return ExitAtMode(); }));

    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{
            [lora_gateway_operation]() { lora_gateway_operation->Notify(); }},
        OnError{[lora_gateway_operation]() { lora_gateway_operation->Failed(); }},
        OnStop{[lora_gateway_operation]() { lora_gateway_operation->Stop(); }}});

    return pipeline;
  }));

  return lora_gateway_operation;
}

ActionPtr<DxSmartLr02LoraGateway::LoraGatewayOperation>
DxSmartLr02LoraGateway::SetLoraModuleCRCCheck(
    kLoraModuleCRCCheck const& crc_check) {
  auto lora_gateway_operation = ActionPtr<LoraGatewayOperation>{action_context_};

  operation_queue_->Push(Stage([this, lora_gateway_operation, crc_check]() {
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        // Enter AT command mode
        Stage([this]() { return EnterAtMode(); }), Stage([this, crc_check]() {
          return at_comm_support_.MakeRequest(
              "AT+CRC" + std::to_string(static_cast<int>(crc_check)), kWaitOk);
        }),
        // Exit AT command mode
        Stage([this]() { return ExitAtMode(); }));

    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{
            [lora_gateway_operation]() { lora_gateway_operation->Notify(); }},
        OnError{[lora_gateway_operation]() { lora_gateway_operation->Failed(); }},
        OnStop{[lora_gateway_operation]() { lora_gateway_operation->Stop(); }}});

    return pipeline;
  }));

  return lora_gateway_operation;
}

ActionPtr<DxSmartLr02LoraGateway::LoraGatewayOperation>
DxSmartLr02LoraGateway::SetLoraGatewayIQSignalInversion(
    kLoraGatewayIQSignalInversion const& signal_inversion) {
  auto lora_gateway_operation = ActionPtr<LoraGatewayOperation>{action_context_};

  operation_queue_->Push(Stage([this, lora_gateway_operation,
                                signal_inversion]() {
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        // Enter AT command mode
        Stage([this]() { return EnterAtMode(); }),
        Stage([this, signal_inversion]() {
          return at_comm_support_.MakeRequest(
              "AT+IQ" + std::to_string(static_cast<int>(signal_inversion)),
              kWaitOk);
        }),
        // Exit AT command mode
        Stage([this]() { return ExitAtMode(); }));

    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{
            [lora_gateway_operation]() { lora_gateway_operation->Notify(); }},
        OnError{[lora_gateway_operation]() { lora_gateway_operation->Failed(); }},
        OnStop{[lora_gateway_operation]() { lora_gateway_operation->Stop(); }}});

    return pipeline;
  }));

  return lora_gateway_operation;
}

// =============================private members=========================== //
void DxSmartLr02LoraGateway::Init() {
  operation_queue_->Push(Stage([this]() {
    auto init_pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        // Enter AT command mode
        Stage([this]() { return EnterAtMode(); }),
        Stage([this]() { return at_comm_support_.MakeRequest("AT", kWaitOk); }),
        Stage([this]() {
          return SetupSerialPort(lora_gateway_init_.serial_init);
        }),
        Stage([this]() { return SetPowerSaveParam(lora_gateway_init_.psp); }),
        Stage([this]() { return SetupLoraNet(lora_gateway_init_); }),
        // Exit AT command mode
        Stage([this]() { return ExitAtMode(); }),
        Stage<GenAction>(action_context_, [this]() {
          initiated_ = true;
          return UpdateStatus::Result();
        }));

    init_pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{[]() { AE_TELED_INFO("DxSmartLr02LoraGateway init success"); }},
        OnError{[]() { AE_TELED_ERROR("DxSmartLr02LoraGateway init failed"); }},
    });

    return init_pipeline;
  }));
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::OpenTcpConnection(
    ActionPtr<OpenNetworkOperation> open_network_operation,
    std::string const& host, std::uint16_t port) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this, open_network_operation{std::move(open_network_operation)},
             host{host}, port]() {
        auto open_operation = ActionPtr<DxSmartLr02TcpOpenNetwork>{
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

ActionPtr<IPipeline> DxSmartLr02LoraGateway::OpenUdpConnection(
    ActionPtr<OpenNetworkOperation> open_network_operation,
    std::string const& host, std::uint16_t port) {
  return MakeActionPtr<Pipeline>(
      action_context_,
      Stage([this, open_network_operation{std::move(open_network_operation)},
             host{host}, port]() {
        auto open_operation = ActionPtr<DxSmartLr02UdpOpenNetwork>{
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

void DxSmartLr02LoraGateway::SetupPoll() {
  poll_listener_ = at_comm_support_.ListenForResponse(
      "#XPOLL: ", [this](auto& at_buffer, auto pos) {
        std::int32_t handle{};
        std::string flags;
        AtSupport::ParseResponse(*pos, "#XPOLL", handle, flags);
        PollEvent(handle, flags);
        return at_buffer.erase(pos);
      });

  // TODO: config for poll interval
  poll_task_ = ActionPtr<RepeatableTask>{
      action_context_,
      [this]() {
        if (connections_.empty()) {
          return;
        }
        // add poll to operation queue
        operation_queue_->Push(Stage([this]() { return Poll(); }));
      },
      std::chrono::milliseconds{100}};
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::Poll() {
  return MakeActionPtr<Pipeline>(action_context_, Stage([this]() {
                                   std::string handles;
                                   for (auto ci : connections_) {
                                     handles += "," + std::to_string(ci);
                                   }
                                   return at_comm_support_.MakeRequest(
                                       "#XPOLL=0" + handles, kWaitOk);
                                 }));
}

void DxSmartLr02LoraGateway::PollEvent(std::int32_t handle,
                                      std::string_view flags) {
  auto flags_val = FromChars<std::uint32_t>(flags);
  if (!flags_val) {
    return;
  }

  // get connection index
  auto it = connections_.find(static_cast<ConnectionLoraGatewayIndex>(handle));
  if (it == std::end(connections_)) {
    AE_TELED_ERROR("Poll unknown handle {}", handle);
    return;
  }

  constexpr std::uint32_t kPollIn = 0x01;
  if (*flags_val | kPollIn) {
    operation_queue_->Push(
        Stage([this, connection{*it}]() { return ReadPacket(connection); }));
  }
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::EnterAtMode() {
  if (at_mode_ == false) {
    at_mode_ = true;
    return MakeActionPtr<Pipeline>(action_context_, Stage([this]() {
                                     return at_comm_support_.MakeRequest(
                                         "+++", kWaitEntryAt);
                                   }));
  }

  return {};
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::ExitAtMode() {
  if (at_mode_ == true) {
    at_mode_ = false;
    return MakeActionPtr<Pipeline>(action_context_, Stage([this]() {
                                     return at_comm_support_.MakeRequest(
                                         "+++", kWaitExitAt, kWaitPowerOn);
                                   }));
  }

  return {};
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::SetLoraModuleMode(
    kLoraModuleMode const& mode) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, mode]() {
        return at_comm_support_.MakeRequest(
            "AT+MODE" + std::to_string(static_cast<int>(mode)), kWaitOk);
      }));
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::SetLoraModuleLevel(
    kLoraModuleLevel const& level) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, level]() {
        return at_comm_support_.MakeRequest(
            "AT+LEVEL" + std::to_string(static_cast<int>(level)), kWaitOk);
      }));
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::SetLoraModulePower(
    kLoraModulePower const& power) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, power]() {
        return at_comm_support_.MakeRequest(
            "AT+POWE" + std::to_string(static_cast<int>(power)), kWaitOk);
      }));
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::SetLoraModuleBandWidth(
    kLoraModuleBandWidth const& band_width) {
  int bw{0};
  if (band_width != kLoraModuleBandWidth::kBandWidth125K) {
    return {};
  }

  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, bw]() {
        return at_comm_support_.MakeRequest(
            "AT+BW" + std::to_string(bw), kWaitOk);
      }));
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::SetLoraModuleCodingRate(
    kLoraModuleCodingRate const& coding_rate) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, coding_rate]() {
        return at_comm_support_.MakeRequest(
            "AT+CR" + std::to_string(static_cast<int>(coding_rate)), kWaitOk);
      }));
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::SetLoraModuleSpreadingFactor(
    kLoraModuleSpreadingFactor const& spreading_factor) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, spreading_factor]() {
        return at_comm_support_.MakeRequest(
            "AT+SF" + std::to_string(static_cast<int>(spreading_factor)),
            kWaitOk);
      }));
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::SetupSerialPort(
    SerialInit& serial_init) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, serial_init]() {
        return SetBaudRate(serial_init.baud_rate);
      }),
      Stage([this, serial_init]() { return SetParity(serial_init.parity); }),
      Stage([this, serial_init]() {
        return SetStopBits(serial_init.stop_bits);
      }));
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::SetBaudRate(kBaudRate baud_rate) {
  auto it = baud_rate_commands_lr02.find(baud_rate);
  if (it == baud_rate_commands_lr02.end()) {
    return {};
  }

  return MakeActionPtr<Pipeline>(action_context_, Stage([this, it]() {
                                   return at_comm_support_.MakeRequest(
                                       it->second, kWaitOk);
                                 }));
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::SetParity(kParity parity) {
  std::string cmd{};

  switch (parity) {
    case kParity::kNoParity:
      cmd = "AT+PARI0";  // Set no parity
      break;
    case kParity::kOddParity:
      cmd = "AT+PARI1";  // Set odd parity
      break;
    case kParity::kEvenParity:
      cmd = "AT+PARI2";  // Set even parity
      break;
    default:
      return {};
      break;
  }

  return MakeActionPtr<Pipeline>(action_context_, Stage([this, cmd]() {
                                   return at_comm_support_.MakeRequest(cmd,
                                                                       kWaitOk);
                                 }));
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::SetStopBits(kStopBits stop_bits) {
  std::string cmd{};

  switch (stop_bits) {
    case kStopBits::kOneStopBit:
      cmd = "AT+STOP1";  // 0 stop bits
      break;
    case kStopBits::kTwoStopBit:
      cmd = "AT+STOP2";  // 2 stop bits
      break;
    default:
      return {};
      break;
  }

  return MakeActionPtr<Pipeline>(action_context_,

                                 Stage([this, cmd]() {
                                   return at_comm_support_.MakeRequest(cmd,
                                                                       kWaitOk);
                                 }));
}

ActionPtr<IPipeline> DxSmartLr02LoraGateway::SetupLoraNet(
    LoraModuleInit& lora_module_init) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, lora_gateway_init]() {
        return SetLoraModuleAddress(lora_gateway_init.lora_module_my_adress);
      }),
      Stage([this, lora_gateway_init]() {
        return SetLoraModuleChannel(lora_gateway_init.lora_module_channel);
      }),
      Stage([this, lora_gateway_init]() {
        return SetLoraModuleCRCCheck(lora_gateway_init.lora_module_crc_check);
      }),
      Stage([this, lora_gateway_init]() {
        return SetLoraModuleIQSignalInversion(
            lora_gateway_init.lora_gateway_signal_inversion);
      }));
}

std::string DxSmartLr02LoraGateway::AdressToString(uint16_t value) {
  uint8_t high = static_cast<uint8_t>((value >> 8));  // High byte
  uint8_t low = static_cast<uint8_t>(value & 0xFF);   // Low byte
  char buffer[7];                                     // Buffer for string

  // Formatting hex string
  std::snprintf(buffer, sizeof(buffer), "%02x,%02x", high, low);

  return std::string(buffer);
}

std::string DxSmartLr02LoraGateway::ChannelToString(uint8_t value) {
  char buffer[5];  // Buffer for string

  // Formatting hex string
  std::snprintf(buffer, sizeof(buffer), "%02x", value);

  return std::string(buffer);
}

}  // namespace ae
