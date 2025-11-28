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

#include "aether/lora_modules/ebyte_e22_lm.h"
#if AE_SUPPORT_LORA && AE_ENABLE_EBYTE_E22_LM

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
static constexpr Duration kTenSeconds = std::chrono::milliseconds{10000};
static const AtRequest::Wait kWaitOk{"OK", kOneSecond};
static const AtRequest::Wait kWaitOkTwoSeconds{"OK", kTwoSeconds};
static const AtRequest::Wait kWaitEntryAt{"Entry AT", kOneSecond};
static const AtRequest::Wait kWaitExitAt{"Exit AT", kOneSecond};
static const AtRequest::Wait kWaitPowerOn{"Power on", kOneSecond};

class EbyteE22LoraOpenNetwork final
    : public Action<EbyteE22LoraOpenNetwork> {
 public:
  EbyteE22LoraOpenNetwork(ActionContext action_context,
                            EbyteE22LoraModule& /* lora_module */,
                            std::string host, std::uint16_t port)
      : Action{action_context},
        action_context_{action_context},
        host_{std::move(host)},
        port_{port} {
    AE_TELED_DEBUG("Open lora connection for {}:{}", host_, port_);
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

EbyteE22LoraModule::EbyteE22LoraModule(ActionContext action_context,
                                       IPoller::ptr const& poller,
                                       LoraModuleInit lora_module_init)
    : action_context_{action_context},
      lora_module_init_{std::move(lora_module_init)},
      serial_{SerialPortFactory::CreatePort(action_context_, std::move(poller),
                                            lora_module_init_.serial_init)},
      at_support_{action_context_, *serial_},
      operation_queue_{action_context_},
      initiated_{false},
      started_{false} {
  Init();
  Start();
}

EbyteE22LoraModule::~EbyteE22LoraModule() { Stop(); }

ActionPtr<EbyteE22LoraModule::LoraModuleOperation> EbyteE22LoraModule::Start() {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation> EbyteE22LoraModule::Stop() {
  return {};
}

ActionPtr<EbyteE22LoraModule::WriteOperation>
EbyteE22LoraModule::WritePacket(ae::ConnectionLoraIndex connect_index,
                                   ae::DataBuffer const& data) {
  if (data.size() > kLoraModuleMTU) {
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

EbyteE22LoraModule::DataEvent::Subscriber
EbyteE22LoraModule::data_event() {
  return EventSubscriber{data_event_};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetPowerSaveParam(LoraPowerSaveParam const& /*psp*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::PowerOff() {
  return {};
}

std::uint16_t EbyteE22LoraModule::GetMtu(){
  return kLoraModuleMTU;
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetLoraModuleAddress(std::uint16_t const& /*address*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetLoraModuleChannel(std::uint8_t const& /*channel*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetLoraModuleCRCCheck(
    kLoraModuleCRCCheck const& /*crc_check*/) {
  return {};
}

ActionPtr<EbyteE22LoraModule::LoraModuleOperation>
EbyteE22LoraModule::SetLoraModuleIQSignalInversion(
    kLoraModuleIQSignalInversion const& /*signal_inversion*/) {
  return {};
}

// ============================private members=============================== //
void EbyteE22LoraModule::Init() {}

ActionPtr<IPipeline> EbyteE22LoraModule::OpenLoraConnection(
    ActionPtr<OpenNetworkOperation> /* open_network_operation */,
    std::string const& /* host */, std::uint16_t /* port */) {
  return {};
}

ActionPtr<IPipeline> EbyteE22LoraModule::SendData(ConnectionLoraIndex /* connection */,
                                              DataBuffer const& /* data */) {
  return {};
}

ActionPtr<IPipeline> EbyteE22LoraModule::ReadPacket(
    ConnectionLoraIndex /* connection */) {
  return {};
}


void EbyteE22LoraModule::SetupPoll() {
  
}

ActionPtr<IPipeline> EbyteE22LoraModule::Poll() {
  return {};
}

void EbyteE22LoraModule::PollEvent(std::int32_t /* handle */,
                                      std::string_view /* flags */) {

}

ActionPtr<IPipeline> EbyteE22LoraModule::EnterAtMode() {
  return {};
}

ActionPtr<IPipeline> EbyteE22LoraModule::ExitAtMode() {
  return {};
}

ActionPtr<IPipeline> EbyteE22LoraModule::SetLoraModuleMode(
    kLoraModuleMode const& mode) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, mode]() {
        return at_support_.MakeRequest(
            "AT+MODE" + std::to_string(static_cast<int>(mode)), kWaitOk);
      }));
}

ActionPtr<IPipeline> EbyteE22LoraModule::SetLoraModuleLevel(
    kLoraModuleLevel const& level) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, level]() {
        return at_support_.MakeRequest(
            "AT+LEVEL" + std::to_string(static_cast<int>(level)), kWaitOk);
      }));
}

ActionPtr<IPipeline> EbyteE22LoraModule::SetLoraModulePower(
    kLoraModulePower const& power) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, power]() {
        return at_support_.MakeRequest(
            "AT+POWE" + std::to_string(static_cast<int>(power)), kWaitOk);
      }));
}

ActionPtr<IPipeline> EbyteE22LoraModule::SetLoraModuleBandWidth(
    kLoraModuleBandWidth const& band_width) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, band_width]() {
        return at_support_.MakeRequest(
            "AT+BW" + std::to_string(static_cast<int>(band_width)), kWaitOk);
      }));
}

ActionPtr<IPipeline> EbyteE22LoraModule::SetLoraModuleCodingRate(
    kLoraModuleCodingRate const& coding_rate) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, coding_rate]() {
        return at_support_.MakeRequest(
            "AT+CR" + std::to_string(static_cast<int>(coding_rate)), kWaitOk);
      }));
}

ActionPtr<IPipeline> EbyteE22LoraModule::SetLoraModuleSpreadingFactor(
    kLoraModuleSpreadingFactor const& spreading_factor) {
  return MakeActionPtr<Pipeline>(
      action_context_, Stage([this, spreading_factor]() {
        return at_support_.MakeRequest(
            "AT+SF" + std::to_string(static_cast<int>(spreading_factor)),
            kWaitOk);
      }));
}

ActionPtr<IPipeline> EbyteE22LoraModule::SetupSerialPort(
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

ActionPtr<IPipeline> EbyteE22LoraModule::SetBaudRate(kBaudRate baud_rate) {
  auto it = baud_rate_commands_e22.find(baud_rate);
  if (it == baud_rate_commands_e22.end()) {
    return {};
  }

  return MakeActionPtr<Pipeline>(action_context_, Stage([this, it]() {
                                   return at_support_.MakeRequest(
                                       it->second, kWaitOk);
                                 }));
}

ActionPtr<IPipeline> EbyteE22LoraModule::SetParity(kParity parity) {
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
                                   return at_support_.MakeRequest(cmd,
                                                                       kWaitOk);
                                 }));
}

ActionPtr<IPipeline> EbyteE22LoraModule::SetStopBits(kStopBits stop_bits) {
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
                                   return at_support_.MakeRequest(cmd,
                                                                       kWaitOk);
                                 }));
}

ActionPtr<IPipeline> EbyteE22LoraModule::SetupLoraNet(
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

std::string EbyteE22LoraModule::AdressToString(uint16_t value) {
  uint8_t high = static_cast<uint8_t>((value >> 8));  // High byte
  uint8_t low = static_cast<uint8_t>(value & 0xFF);   // Low byte
  char buffer[7];                                     // Buffer for string

  // Formatting hex string
  std::snprintf(buffer, sizeof(buffer), "%02x,%02x", high, low);

  return std::string(buffer);
}

std::string EbyteE22LoraModule::ChannelToString(uint8_t value) {
  char buffer[5];  // Buffer for string

  // Formatting hex string
  std::snprintf(buffer, sizeof(buffer), "%02x", value);

  return std::string(buffer);
}
}  // namespace ae
#endif
