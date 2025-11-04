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

#include "aether/lora_modules/dx_smart_lr02_433_lm.h"
#if AE_SUPPORT_LORA && AE_ENABLE_DX_SMART_LR02_433_LM

#  include <bitset>
#  include <string_view>

#  include "aether/misc/defer.h"
#  include "aether/misc/from_chars.h"
#  include "aether/actions/pipeline.h"
#  include "aether/actions/gen_action.h"
#  include "aether/mstream_buffers.h"
#  include "aether/serial_ports/serial_port_factory.h"

#  include "aether/lora_modules/lora_modules_tele.h"

namespace ae {
static constexpr Duration kOneSecond = std::chrono::milliseconds{1000};
static constexpr Duration kTwoSeconds = std::chrono::milliseconds{2000};
static const AtRequest::Wait kWaitOk{"OK", kOneSecond};
static const AtRequest::Wait kWaitOkTwoSeconds{"OK", kTwoSeconds};
static const AtRequest::Wait kWaitEntryAt{"Entry AT", kOneSecond};
static const AtRequest::Wait kWaitExitAt{"Exit AT", kOneSecond};
static const AtRequest::Wait kWaitPowerOn{"Power on", kOneSecond};

class DxSmartLr02TcpOpenNetwork final
    : public Action<DxSmartLr02TcpOpenNetwork> {
 public:
  DxSmartLr02TcpOpenNetwork(ActionContext action_context,
                            DxSmartLr02LoraModule& /* lora_module */,
                            std::string host, std::uint16_t port)
      : Action{action_context},
        action_context_{action_context},
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

  ConnectionLoraIndex connection_index() const { return connection_index_; }

 private:
  ActionContext action_context_;
  std::string host_;
  std::uint16_t port_;
  ActionPtr<IPipeline> operation_pipeline_;
  Subscription operation_sub_;
  ConnectionLoraIndex connection_index_ = kInvalidConnectionLoraIndex;
  bool success_{};
  bool error_{};
  bool stop_{};
};

class DxSmartLr02UdpOpenNetwork final
    : public Action<DxSmartLr02UdpOpenNetwork> {
 public:
  DxSmartLr02UdpOpenNetwork(ActionContext action_context,
                            DxSmartLr02LoraModule& /* lora_module */,
                            std::string host, std::uint16_t port)
      : Action{action_context},
        action_context_{action_context},
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

  ConnectionLoraIndex connection_index() const { return connection_index_; }

 private:
  ActionContext action_context_;
  std::string host_;
  std::uint16_t port_;
  ActionPtr<IPipeline> operation_pipeline_;
  Subscription operation_sub_;
  ConnectionLoraIndex connection_index_ = kInvalidConnectionLoraIndex;
  bool success_{};
  bool error_{};
  bool stop_{};
};

DxSmartLr02LoraModule::DxSmartLr02LoraModule(ActionContext action_context,
                                             IPoller::ptr const& poller,
                                             LoraModuleInit lora_module_init)
    : action_context_{action_context},
      lora_module_init_{std::move(lora_module_init)},
      serial_{SerialPortFactory::CreatePort(action_context_, poller,
                                            lora_module_init_.serial_init)},
      at_comm_support_{action_context_, *serial_},
      operation_queue_{action_context_},
      initiated_{false},
      started_{false} {
  Init();
}

DxSmartLr02LoraModule::~DxSmartLr02LoraModule() { Stop(); }

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::Start() {
  auto lora_module_operation = ActionPtr<LoraModuleOperation>{action_context_};
  operation_queue_->Push(Stage([this, lora_module_operation]()
                                   -> ActionPtr<IPipeline> {
    // if already started, notify of success and return
    if (started_) {
      return MakeActionPtr<Pipeline>(
          action_context_,
          Stage<GenAction>(action_context_, [lora_module_operation]() {
            lora_module_operation->Notify();
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
            [lora_module_operation]() { lora_module_operation->Notify(); }},
        OnError{[lora_module_operation]() { lora_module_operation->Failed(); }},
        OnStop{[lora_module_operation]() { lora_module_operation->Stop(); }}});

    return pipeline;
  }));

  return lora_module_operation;
}

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::Stop() {
  auto lora_module_operation = ActionPtr<LoraModuleOperation>{action_context_};

  operation_queue_->Push(Stage([this, lora_module_operation]() {
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
            [lora_module_operation]() { lora_module_operation->Notify(); }},
        OnError{[lora_module_operation]() { lora_module_operation->Failed(); }},
        OnStop{[lora_module_operation]() { lora_module_operation->Stop(); }}});

    return pipeline;
  }));

  return lora_module_operation;
}

ActionPtr<DxSmartLr02LoraModule::OpenNetworkOperation>
DxSmartLr02LoraModule::OpenNetwork(ae::Protocol protocol,
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

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::CloseNetwork(ae::ConnectionLoraIndex /*connect_index*/) {
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

ActionPtr<DxSmartLr02LoraModule::WriteOperation>
DxSmartLr02LoraModule::WritePacket(ae::ConnectionLoraIndex /*connect_index*/,
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

ActionPtr<IPipeline> DxSmartLr02LoraModule::ReadPacket(
    ConnectionLoraIndex /* connection */) {
  return {};
}

DxSmartLr02LoraModule::DataEvent::Subscriber
DxSmartLr02LoraModule::data_event() {
  return EventSubscriber{data_event_};
}

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetPowerSaveParam(LoraPowerSaveParam const& psp) {
  auto lora_module_operation = ActionPtr<LoraModuleOperation>{action_context_};

  operation_queue_->Push(Stage([this, lora_module_operation, psp{psp}]() {
    auto pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        // Enter AT command mode
        Stage([this]() { return EnterAtMode(); }),
        // Configure Lora Module Mode
        Stage(
            [this, psp]() { return SetLoraModuleMode(psp.lora_module_mode); }),
        // Configure Lora Module Level
        Stage([this, psp]() {
          return SetLoraModuleLevel(psp.lora_module_level);
        }),
        // Configure Lora Module Power
        Stage([this, psp]() {
          return SetLoraModulePower(psp.lora_module_power);
        }),
        // Configure Lora Module BandWidth
        Stage([this, psp]() {
          return SetLoraModuleBandWidth(psp.lora_module_band_width);
        }),
        // Configure Lora Module Coding Rate
        Stage([this, psp]() {
          return SetLoraModuleCodingRate(psp.lora_module_coding_rate);
        }),
        // Configure Lora Module Spreading Factor
        Stage([this, psp]() {
          return SetLoraModuleSpreadingFactor(psp.lora_module_spreading_factor);
        }),
        // Exit AT command mode
        Stage([this]() { return ExitAtMode(); }));
    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{
            [lora_module_operation]() { lora_module_operation->Notify(); }},
        OnError{[lora_module_operation]() { lora_module_operation->Failed(); }},
        OnStop{[lora_module_operation]() { lora_module_operation->Stop(); }}});

    return pipeline;
  }));

  return lora_module_operation;
}

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::PowerOff() {
  return {};
}

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetLoraModuleAddress(std::uint16_t const& address) {
  auto lora_module_operation = ActionPtr<LoraModuleOperation>{action_context_};

  operation_queue_->Push(Stage([this, lora_module_operation, address]() {
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
            [lora_module_operation]() { lora_module_operation->Notify(); }},
        OnError{[lora_module_operation]() { lora_module_operation->Failed(); }},
        OnStop{[lora_module_operation]() { lora_module_operation->Stop(); }}});

    return pipeline;
  }));

  return lora_module_operation;
}

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetLoraModuleChannel(std::uint8_t const& channel) {
  if (channel > 0x1E) {
    return {};
  }

  auto lora_module_operation = ActionPtr<LoraModuleOperation>{action_context_};

  operation_queue_->Push(Stage([this, lora_module_operation, channel]() {
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
            [lora_module_operation]() { lora_module_operation->Notify(); }},
        OnError{[lora_module_operation]() { lora_module_operation->Failed(); }},
        OnStop{[lora_module_operation]() { lora_module_operation->Stop(); }}});

    return pipeline;
  }));

  return lora_module_operation;
}

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetLoraModuleCRCCheck(
    kLoraModuleCRCCheck const& crc_check) {
  auto lora_module_operation = ActionPtr<LoraModuleOperation>{action_context_};

  operation_queue_->Push(Stage([this, lora_module_operation, crc_check]() {
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
            [lora_module_operation]() { lora_module_operation->Notify(); }},
        OnError{[lora_module_operation]() { lora_module_operation->Failed(); }},
        OnStop{[lora_module_operation]() { lora_module_operation->Stop(); }}});

    return pipeline;
  }));

  return lora_module_operation;
}

ActionPtr<DxSmartLr02LoraModule::LoraModuleOperation>
DxSmartLr02LoraModule::SetLoraModuleIQSignalInversion(
    kLoraModuleIQSignalInversion const& signal_inversion) {
  auto lora_module_operation = ActionPtr<LoraModuleOperation>{action_context_};

  operation_queue_->Push(Stage([this, lora_module_operation,
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
            [lora_module_operation]() { lora_module_operation->Notify(); }},
        OnError{[lora_module_operation]() { lora_module_operation->Failed(); }},
        OnStop{[lora_module_operation]() { lora_module_operation->Stop(); }}});

    return pipeline;
  }));

  return lora_module_operation;
}

// =============================private members=========================== //
void DxSmartLr02LoraModule::Init() {
  operation_queue_->Push(Stage([this]() {
    auto init_pipeline = MakeActionPtr<Pipeline>(
        action_context_,
        // Enter AT command mode
        Stage([this]() { return EnterAtMode(); }),
        Stage([this]() { return at_comm_support_.MakeRequest("AT", kWaitOk); }),
        Stage([this]() {
          return SetupSerialPort(lora_module_init_.serial_init);
        }),
        Stage([this]() { return SetPowerSaveParam(lora_module_init_.psp); }),
        Stage([this]() { return SetupLoraNet(lora_module_init_); }),
        // Exit AT command mode
        Stage([this]() { return ExitAtMode(); }),
        Stage<GenAction>(action_context_, [this]() {
          initiated_ = true;
          return UpdateStatus::Result();
        }));

    init_pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{[]() { AE_TELED_INFO("DxSmartLr02LoraModule init success"); }},
        OnError{[]() { AE_TELED_ERROR("DxSmartLr02LoraModule init failed"); }},
    });

    return init_pipeline;
  }));
}

ActionPtr<IPipeline> DxSmartLr02LoraModule::OpenTcpConnection(
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

ActionPtr<IPipeline> DxSmartLr02LoraModule::OpenUdpConnection(
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

void DxSmartLr02LoraModule::SetupPoll() {
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

ActionPtr<IPipeline> DxSmartLr02LoraModule::Poll() {
  return MakeActionPtr<Pipeline>(action_context_, Stage([this]() {
                                   std::string handles;
                                   for (auto ci : connections_) {
                                     handles += "," + std::to_string(ci);
                                   }
                                   return at_comm_support_.MakeRequest(
                                       "#XPOLL=0" + handles, kWaitOk);
                                 }));
}

void DxSmartLr02LoraModule::PollEvent(std::int32_t handle,
                                      std::string_view flags) {
  auto flags_val = FromChars<std::uint32_t>(flags);
  if (!flags_val) {
    return;
  }

  // get connection index
  auto it = connections_.find(static_cast<ConnectionLoraIndex>(handle));
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

ActionPtr<IPipeline> DxSmartLr02LoraModule::EnterAtMode() {
  if (at_mode_ == false) {
    at_mode_ = true;
    return MakeActionPtr<Pipeline>(action_context_, Stage([this]() {
                                     return at_comm_support_.MakeRequest(
                                         "+++", kWaitEntryAt);
                                   }));
  }

  return {};
}

ActionPtr<IPipeline> DxSmartLr02LoraModule::ExitAtMode() {
  if (at_mode_ == true) {
    at_mode_ = false;
    return MakeActionPtr<Pipeline>(action_context_, Stage([this]() {
                                     return at_comm_support_.MakeRequest(
                                         "+++", kWaitExitAt, kWaitPowerOn);
                                   }));
  }

  return {};
}

ActionPtr<IPipeline> DxSmartLr02LoraModule::SetLoraModuleMode(
    kLoraModuleMode const& mode) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, mode]() {
        return at_comm_support_.MakeRequest(
            "AT+MODE" + std::to_string(static_cast<int>(mode)), kWaitOk);
      }));
}

ActionPtr<IPipeline> DxSmartLr02LoraModule::SetLoraModuleLevel(
    kLoraModuleLevel const& level) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, level]() {
        return at_comm_support_.MakeRequest(
            "AT+LEVEL" + std::to_string(static_cast<int>(level)), kWaitOk);
      }));
}

ActionPtr<IPipeline> DxSmartLr02LoraModule::SetLoraModulePower(
    kLoraModulePower const& power) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, power]() {
        return at_comm_support_.MakeRequest(
            "AT+POWE" + std::to_string(static_cast<int>(power)), kWaitOk);
      }));
}

ActionPtr<IPipeline> DxSmartLr02LoraModule::SetLoraModuleBandWidth(
    kLoraModuleBandWidth const& band_width) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, band_width]() {
        return at_comm_support_.MakeRequest(
            "AT+BW" + std::to_string(static_cast<int>(band_width)), kWaitOk);
      }));
}

ActionPtr<IPipeline> DxSmartLr02LoraModule::SetLoraModuleCodingRate(
    kLoraModuleCodingRate const& coding_rate) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, coding_rate]() {
        return at_comm_support_.MakeRequest(
            "AT+CR" + std::to_string(static_cast<int>(coding_rate)), kWaitOk);
      }));
}

ActionPtr<IPipeline> DxSmartLr02LoraModule::SetLoraModuleSpreadingFactor(
    kLoraModuleSpreadingFactor const& spreading_factor) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, spreading_factor]() {
        return at_comm_support_.MakeRequest(
            "AT+SF" + std::to_string(static_cast<int>(spreading_factor)),
            kWaitOk);
      }));
}

ActionPtr<IPipeline> DxSmartLr02LoraModule::SetupSerialPort(
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

ActionPtr<IPipeline> DxSmartLr02LoraModule::SetBaudRate(kBaudRate baud_rate) {
  auto it = baud_rate_commands_lr02.find(baud_rate);
  if (it == baud_rate_commands_lr02.end()) {
    return {};
  }

  return MakeActionPtr<Pipeline>(action_context_, Stage([this, it]() {
                                   return at_comm_support_.MakeRequest(
                                       it->second, kWaitOk);
                                 }));
}

ActionPtr<IPipeline> DxSmartLr02LoraModule::SetParity(kParity parity) {
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

ActionPtr<IPipeline> DxSmartLr02LoraModule::SetStopBits(kStopBits stop_bits) {
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

ActionPtr<IPipeline> DxSmartLr02LoraModule::SetupLoraNet(
    LoraModuleInit& lora_module_init) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, lora_module_init]() {
        return SetLoraModuleAddress(lora_module_init.lora_module_my_adress);
      }),
      Stage([this, lora_module_init]() {
        return SetLoraModuleChannel(lora_module_init.lora_module_channel);
      }),
      Stage([this, lora_module_init]() {
        return SetLoraModuleCRCCheck(lora_module_init.lora_module_crc_check);
      }),
      Stage([this, lora_module_init]() {
        return SetLoraModuleIQSignalInversion(
            lora_module_init.lora_module_signal_inversion);
      }));
}

std::string DxSmartLr02LoraModule::AdressToString(uint16_t value) {
  uint8_t high = static_cast<uint8_t>((value >> 8));  // High byte
  uint8_t low = static_cast<uint8_t>(value & 0xFF);   // Low byte
  char buffer[7];                                     // Buffer for string

  // Formatting hex string
  std::snprintf(buffer, sizeof(buffer), "%02x,%02x", high, low);

  return std::string(buffer);
}

std::string DxSmartLr02LoraModule::ChannelToString(uint8_t value) {
  char buffer[5];  // Buffer for string

  // Formatting hex string
  std::snprintf(buffer, sizeof(buffer), "%02x", value);

  return std::string(buffer);
}

}  // namespace ae
#endif
