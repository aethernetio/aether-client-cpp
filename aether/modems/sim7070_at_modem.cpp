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

#  include "aether-miscpp/misc/override.h"
#  include "aether/executors/executors.h"
#  include "aether/serial_ports/at_support/at_stage.h"
#  include "aether/serial_ports/at_support/at_request.h"
#  include "aether/serial_ports/serial_port_factory.h"

#  include "aether/modems/modems_tele.h"

namespace ae {
static constexpr auto kWaitOk = at::Wait{"OK"};

namespace sim7070_modem_internal {
OpenNetworkOperationImpl::OpenNetworkOperationImpl(AeContext const& ae_context,
                                                   Sim7070AtModem& self,
                                                   Protocol protocol,
                                                   std::string host,
                                                   std::uint16_t port)
    : ae_context_{ae_context},
      self_{&self},
      at_support_{self.at_support_},
      protocol_{protocol},
      host_{std::move(host)},
      port_{port} {
  AE_TELED_DEBUG("Open {} connection for {}:{}",
                 protocol == Protocol::kTcp ? "tcp" : "udp", host_, port_);
  RunPipeline();
}

auto OpenNetworkOperationImpl::Pipeline() {
  // AT+CAOPEN=<cid>,<pdp_index>,<conn_type>,<server>,<port>[,<recv_mode>]
  auto handle = self_->next_connection_index_++;
  auto protocol_str = [&]() -> std::string_view {
    if (protocol_ == Protocol::kTcp) {
      return "TCP";
    }
    if (protocol_ == Protocol::kUdp) {
      return "UDP";
    }
    return "UNKNOWN";
  }();

  return ex::just() |
         at::MakeRequest(at_support_,
                         Format(R"(AT+CAOPEN={},0,"{}","{}",{})", handle,
                                protocol_str, host_, port_),
                         at::Wait{Format("+CAOPEN: {},0", handle),
                                  [handle](AtBuffer&, auto pos) {
                                    std::int32_t result_code{};
                                    int expected_handle =
                                        static_cast<int>(handle);
                                    if (at_support::ParseResponse(
                                            *pos, "+CAOPEN", result_code,
                                            expected_handle)) {
                                      return result_code == 0;
                                    }
                                    return false;
                                  }}) |
         ex::with_timeout(ae_context_, 10s);
}

void OpenNetworkOperationImpl::RunPipeline() {
  self_->operation_queue_.Push(at::Stage(ae_context_, [this]() {
    return Pipeline() | ex::then([this]() noexcept {
             AE_TELED_DEBUG("Opened connection {}", handle_);
             self_->connections_.emplace(static_cast<ConnectionIndex>(handle_));
             SetResult(Ok{static_cast<ConnectionIndex>(handle_)});
           }) |
           ex::upon_error(Override{
               [&](ex::TimeoutError) noexcept {
                 SetResult(Error{static_cast<ModemError>(-2)});
               },
               [&](std::exception_ptr) noexcept {
                 SetResult(Error{static_cast<ModemError>(-3)});
               },
               [&](auto err) noexcept {
                 SetResult(Error{static_cast<ModemError>(err)});
               },
           });
  }));
}

CloseNetworkOperationImpl::CloseNetworkOperationImpl(
    AeContext const& ae_context, Sim7070AtModem& self,
    ConnectionIndex connect_index)
    : ae_context_{ae_context},
      self_{&self},
      at_support_{self.at_support_},
      connect_index_{connect_index} {
  AE_TELED_DEBUG("Close network connection {}", connect_index_);
  RunPipeline();
}

auto CloseNetworkOperationImpl::Pipeline() {
  // AT+CACLOSE=<cid> // Close TCP/UDP socket
  return ex::just() |
         at::MakeRequest(
             at_support_,
             "AT+CACLOSE=" +
                 std::to_string(static_cast<std::int32_t>(connect_index_)),
             kWaitOk) |
         ex::with_timeout(ae_context_, 10s);
}

void CloseNetworkOperationImpl::RunPipeline() {
  self_->operation_queue_.Push(at::Stage(ae_context_, [this]() {
    return Pipeline() | ex::then([this]() noexcept {
             self_->connections_.erase(connect_index_);
             SetResult(Ok{kIgnore});
           }) |
           ex::upon_error(Override{
               [&](ex::TimeoutError) noexcept {
                 SetResult(Error{static_cast<ModemError>(-2)});
               },
               [&](std::exception_ptr const&) noexcept {
                 SetResult(Error{static_cast<ModemError>(-3)});
               },
               [&](auto err) noexcept {
                 SetResult(Error{static_cast<ModemError>(err)});
               },
           });
  }));
}

WriteOperationImpl::WriteOperationImpl(AeContext const& ae_context,
                                       Sim7070AtModem& self,
                                       ConnectionIndex connect_index,
                                       std::span<std::uint8_t const> data)
    : ae_context_{ae_context},
      self_{&self},
      at_support_{self.at_support_},
      connect_index_{connect_index},
      data_{data} {
  AE_TELED_DEBUG("Write to connection {} ({} bytes)", connect_index_,
                 data_.size());
  RunPipeline();
}

auto WriteOperationImpl::Pipeline() {
  auto handle = static_cast<std::int32_t>(connect_index_);

  // AT+CASEND=<cid>,<datalen>[,<inputtime>]
  return ex::just() |
         at::MakeRequest(at_support_,
                         "AT+CASEND=" + std::to_string(handle) + "," +
                             std::to_string(data_.size()),
                         at::Wait{">"}) |
         at::MakeRequest(
             at_support_,
             [this]() noexcept -> Result<std::size_t, int> {
               self_->serial_->Write(data_);
               return Ok{data_.size()};
             },
             kWaitOk) |
         ex::with_timeout(ae_context_, 10s);
}

void WriteOperationImpl::RunPipeline() {
  self_->operation_queue_.Push(at::Stage(ae_context_, [this]() {
    return Pipeline() |
           ex::then([this]() noexcept { SetResult(Ok{data_.size()}); }) |
           ex::upon_error(Override{
               [&](ex::TimeoutError) noexcept {
                 SetResult(Error{static_cast<ModemError>(-2)});
               },
               [&](std::exception_ptr const&) noexcept {
                 SetResult(Error{static_cast<ModemError>(-3)});
               },
               [&](auto err) noexcept {
                 SetResult(Error{static_cast<ModemError>(err)});
               },
           });
  }));
}

ModemStartOperation::ModemStartOperation(AeContext const& ae_context,
                                         Sim7070AtModem& self)
    : ae_context_{ae_context},
      self_{&self},
      modem_init_{self.modem_init_},
      at_support_{self.at_support_} {
  RunPipeline();
}

auto ModemStartOperation::SetBaudRate(kBaudRate const rate) {
  // TODO: maybe use flat map instead
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

  return ex::let_value([&, rate]() noexcept {
    auto make_request = [&](std::string&& cmd) noexcept {
      return at::MakeRequest(ex::just(), at_support_, std::move(cmd), kWaitOk) |
             ex::with_timeout(ae_context_, 1s);
    };

    using res = ex::variant_sender<
        std::invoke_result_t<ex::just_error_t, int>,
        std::invoke_result_t<decltype(make_request), std::string&&>>;

    auto it = baud_rate_commands_sim7070.find(rate);
    if (it == baud_rate_commands_sim7070.end()) {
      return res{ex::just_error(1)};
    }
    return res{make_request(std::string{it->second})};
  });
}

auto ModemStartOperation::SetNetMode(kModemMode const modem_mode) {
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

  return ex::let_value([&, modem_mode]() noexcept {
    auto make_request = [&](std::string&& cmd) noexcept {
      return at::MakeRequest(ex::just(), at_support_, std::move(cmd), kWaitOk) |
             ex::with_timeout(ae_context_, 1s);
    };

    using res = ex::variant_sender<
        std::invoke_result_t<ex::just_error_t, int>,
        std::invoke_result_t<decltype(make_request), std::string&&>>;

    auto it = net_mode_commands_sim7070.find(modem_mode);
    if (it == net_mode_commands_sim7070.end()) {
      return res{ex::just_error(2)};
    }
    return res{make_request(std::string{it->second})};
  });
}

auto ModemStartOperation::SetupNetwork(
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

  auto cmd = std::invoke([&]() noexcept -> std::string {
    if (!operator_name.empty()) {
      // Operator long name
      return "AT+COPS=1,0,\"" + operator_name + "\"," + mode;
    }
    if (!operator_code.empty()) {
      return "AT+COPS=1,2,\"" + operator_code + "\"," + mode;  // Operator code
    }
    return "AT+COPS=0";  // Auto
  });

  return at::MakeRequest(at_support_, std::move(cmd), at::Wait{"OK"}) |
         ex::with_timeout(ae_context_, 120s) |
         at::MakeRequest(at_support_,
                         R"(AT+CGDCONT=1,"IP",")" + apn_name + "\"", kWaitOk) |
         ex::with_timeout(ae_context_, 1s) |
         at::MakeRequest(at_support_,
                         "AT+CNCFG=0,0,\"" + apn_name + "\",\"" + apn_user +
                             "\",\"" + apn_pass + "\"," + type,
                         kWaitOk) |
         ex::with_timeout(ae_context_, 1s) |
         at::MakeRequest(at_support_, "AT+CREG=1;+CGREG=1;+CEREG=1", kWaitOk) |
         ex::with_timeout(ae_context_, 1s) |
         at::MakeRequest(at_support_,
                         "AT+CNACT=0,1",  // Activate the PDP context
                         kWaitOk) |
         ex::with_timeout(ae_context_, 30s);
}

auto ModemStartOperation::CheckSimStatus() {
  return at::MakeRequest(at_support_, "AT+CPIN?", kWaitOk) |
         ex::with_timeout(ae_context_, 1s);
}

auto ModemStartOperation::SetupSim(std::uint16_t pin) {
  return ex::let_value([&, pin]() noexcept {
    auto pin_string = at_support::PinToString(pin);

    auto make_request = [&]() noexcept {
      return at::MakeRequest(ex::just(), at_support_, "AT+CPIN=" + pin_string,
                             kWaitOk) |
             ex::with_timeout(ae_context_, 1s);
    };

    using res =
        ex::variant_sender<std::invoke_result_t<decltype(ex::just_error), int>,
                           std::invoke_result_t<decltype(make_request)>>;

    if (pin_string.empty()) {
      AE_TELED_ERROR("Pin wrong!");
      return res{ex::just_error(3)};
    }
    return res{make_request()};
  });
}

auto ModemStartOperation::Pipeline() {
  return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
             [&](auto& ctx) noexcept {
               if (self_->initiated_) {
                 ex::set_value(std::move(ctx.receiver));
               } else {
                 AE_TELED_ERROR("Modem not initiated");
                 ex::set_error(std::move(ctx.receiver), -1);
               }
             }) |
         CheckSimStatus() | ex::let_value([&]() noexcept {
           auto setup_sim = [this]() noexcept {
             return ex::just() | SetupSim(modem_init_.use_pin);
           };
           using res =
               ex::variant_sender<std::invoke_result_t<decltype(ex::just)>,
                                  std::invoke_result_t<decltype(setup_sim)>>;

           if (modem_init_.use_pin) {
             return res{setup_sim()};
           }
           return res{ex::just()};
         }) |
         at::MakeRequest(at_support_, "AT+CFUN=1,0", kWaitOk) |
         ex::with_timeout(ae_context_, 1s) |
         SetBaudRate(modem_init_.serial_init.baud_rate) |
         SetNetMode(modem_init_.modem_mode) |
         SetupNetwork(modem_init_.operator_name, modem_init_.operator_code,
                      modem_init_.apn_name, modem_init_.apn_user,
                      modem_init_.apn_pass, modem_init_.modem_mode,
                      modem_init_.auth_type);
}

void ModemStartOperation::RunPipeline() {
  // all AT operations must be in operation_queue_
  self_->operation_queue_.Push(at::Stage(ae_context_, [this]() noexcept {
    return Pipeline() | ex::then([&]() noexcept {
             self_->started_ = true;
             self_->SetupPoll();
             SetResult(Ok{kIgnore});
           }) |
           ex::upon_error(Override{
               [&](ex::TimeoutError) noexcept {
                 AE_TELED_ERROR("ModemStartOperation timeout");
                 SetResult(Error{static_cast<ModemError>(-2)});
               },
               [&](std::exception_ptr const&) noexcept {
                 SetResult(Error{static_cast<ModemError>(-3)});
               },
               [&](auto err) noexcept {
                 AE_TELED_ERROR("ModemStartOperation error: {}", err);
                 SetResult(Error{static_cast<ModemError>(err)});
               },
           });
  }));
}

ModemStopOperation::ModemStopOperation(AeContext const& ae_context,
                                       Sim7070AtModem& self)
    : ae_context_{ae_context}, self_{&self}, at_support_{self.at_support_} {
  RunPipeline();
}

auto ModemStopOperation::Pipeline() {
  return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
             [&](auto& ctx) noexcept {
               if (!self_->started_) {
                 ex::set_error(std::move(ctx.receiver), -1);
                 return;
               }
               ex::set_value(std::move(ctx.receiver));
             }) |
         at::MakeRequest(at_support_, "AT+CNACT=0,0", kWaitOk) |
         ex::with_timeout(ae_context_, 10s) |
         at::MakeRequest(at_support_, "", at::Wait{"+APP PDP: 0,DEACTIVE"},
                         kWaitOk) |
         ex::with_timeout(ae_context_, 10s) |
         at::MakeRequest(at_support_, "ATZ", kWaitOk) |
         ex::with_timeout(ae_context_, 1s) |
         at::MakeRequest(at_support_, "AT+CFUN=0", kWaitOk) |
         ex::with_timeout(ae_context_, 1s);
}

void ModemStopOperation::RunPipeline() {
  self_->operation_queue_.Push(at::Stage(ae_context_, [this]() {
    return Pipeline() | ex::then([&]() noexcept {
             self_->started_ = false;
             SetResult(Ok{kIgnore});
           }) |
           ex::upon_error(Override{
               [&](ex::TimeoutError) noexcept {
                 SetResult(Error{static_cast<ModemError>(-2)});
               },
               [&](std::exception_ptr const&) noexcept {
                 SetResult(Error{static_cast<ModemError>(-3)});
               },
               [&](auto err) noexcept {
                 SetResult(Error{static_cast<ModemError>(err)});
               },
           });
  }));
}

ModemSetPowerSaveParamOperation::ModemSetPowerSaveParamOperation(
    AeContext const& ae_context, Sim7070AtModem& self, ModemPowerSaveParam psp)
    : ae_context_{ae_context}, self_{&self}, psp_{std::move(psp)} {
  RunPipeline();
}

auto ModemSetPowerSaveParamOperation::Pipeline() {
  return ex::just() | ex::with_timeout(ae_context_, 1s);
}

void ModemSetPowerSaveParamOperation::RunPipeline() {
  self_->operation_queue_.Push(at::Stage(ae_context_, [this]() noexcept {
    return Pipeline() | ex::then([&]() noexcept { SetResult(Ok{kIgnore}); }) |
           ex::upon_error(Override{
               [&](ex::TimeoutError) noexcept {
                 SetResult(Error{static_cast<ModemError>(-2)});
               },
               [&](std::exception_ptr const&) noexcept {
                 SetResult(Error{static_cast<ModemError>(-3)});
               },
               [&](auto err) noexcept {
                 SetResult(Error{static_cast<ModemError>(err)});
               },
           });
  }));
}

ModemPowerOffOperation::ModemPowerOffOperation(AeContext const& ae_context,
                                               Sim7070AtModem& self)
    : ae_context_{ae_context}, self_{&self}, at_support_{self.at_support_} {
  RunPipeline();
}

auto ModemPowerOffOperation::Pipeline() {
  return ex::just() | at::MakeRequest(at_support_, "AT+CFUN=1", kWaitOk) |
         ex::with_timeout(ae_context_, 1s) |
         at::MakeRequest(at_support_, "AT+CPOWD=1", kWaitOk) |
         ex::with_timeout(ae_context_, 5s);
}

void ModemPowerOffOperation::RunPipeline() {
  self_->operation_queue_.Push(at::Stage(ae_context_, [this]() {
    return Pipeline() | ex::then([&]() noexcept { SetResult(Ok{kIgnore}); }) |
           ex::upon_error(Override{
               [&](ex::TimeoutError) noexcept {
                 SetResult(Error{static_cast<ModemError>(-2)});
               },
               [&](auto error) noexcept {
                 SetResult(Error{static_cast<ModemError>(error)});
               },
           });
  }));
}
}  // namespace sim7070_modem_internal

Sim7070AtModem::Sim7070AtModem(AeContext const& ae_context,
                               IPoller::ptr const& poller, ModemInit modem_init)
    : ae_context_{ae_context},
      modem_init_{std::move(modem_init)},
      serial_{SerialPortFactory::CreatePort(ae_context_, poller,
                                            modem_init_.serial_init)},
      at_support_{*serial_},
      operation_queue_{},
      open_network_pool_{ae_context_},
      close_network_pool_{ae_context_},
      write_pool_{ae_context_},
      initiated_{false},
      started_{false} {
  AE_TELED_DEBUG("Sim7070AtModem init");
  Init();
}

void Sim7070AtModem::Init() {
  operation_queue_.Push(at::Stage(ae_context_, [this]() {
    return at::MakeRequest(ex::just(), at_support_, "AT", kWaitOk) |
           at::MakeRequest(at_support_, "ATE0", kWaitOk) |
           at::MakeRequest(at_support_, "AT+CMEE=1", kWaitOk) |
           ex::with_timeout(ae_context_, 1s) | ex::then([&]() noexcept {
             AE_TELED_INFO("Sim7070AtModem init success");
             initiated_ = true;
           }) |
           ex::upon_error(Override{
               [](ex::TimeoutError) {
                 AE_TELED_ERROR("Sim7070AtModem init failed, with timeout");
               },
               [](int error) {
                 AE_TELED_ERROR("Sim7070AtModem init failed, with error {}",
                                error);
               },
           });
  }));
}

ModemOperation* Sim7070AtModem::Start() {
  if (!modem_start_operation_ || modem_start_operation_->is_finished()) {
    if (started_) {
      modem_start_operation_ = std::make_unique<
          sim7070_modem_internal::ModemStartedAlreadyOperation>();
    } else {
      modem_start_operation_ =
          std::make_unique<sim7070_modem_internal::ModemStartOperation>(
              ae_context_, *this);
    }
  }

  return modem_start_operation_.get();
}

ModemOperation* Sim7070AtModem::Stop() {
  if (!modem_stop_operation_ || modem_stop_operation_->is_finished()) {
    if (!started_) {
      modem_stop_operation_ = std::make_unique<
          sim7070_modem_internal::ModemStoppedAlreadyOperation>();
    } else {
      modem_stop_operation_ =
          std::make_unique<sim7070_modem_internal::ModemStopOperation>(
              ae_context_, *this);
    }
  }

  return modem_stop_operation_.get();
}

OpenNetworkOperation* Sim7070AtModem::OpenNetwork(Protocol protocol,
                                                  std::string const& host,
                                                  std::uint16_t port) {
  // setup polling on demand
  if (!poll_listener_) {
    SetupPoll();
  }

  auto* op =
      open_network_pool_.Create(ae_context_, *this, protocol, host, port);
  return op;
}

ModemOperation* Sim7070AtModem::CloseNetwork(ConnectionIndex connect_index) {
  auto* op = close_network_pool_.Create(ae_context_, *this, connect_index);
  return op;
}

WriteOperation* Sim7070AtModem::WritePacket(
    ConnectionIndex connect_index, std::span<std::uint8_t const> data) {
  if (data.size() > kModemMTU) {
    assert(false);
    return nullptr;
  }

  auto* op = write_pool_.Create(ae_context_, *this, connect_index, data);
  return op;
}

Sim7070AtModem::DataEvent::Subscriber Sim7070AtModem::data_event() {
  return EventSubscriber{data_event_};
}

ModemOperation* Sim7070AtModem::SetPowerSaveParam(
    ModemPowerSaveParam const& psp) {
  if (!modem_set_psp_operation_ || modem_set_psp_operation_->is_finished()) {
    modem_set_psp_operation_ = std::make_unique<
        sim7070_modem_internal::ModemSetPowerSaveParamOperation>(ae_context_,
                                                                 *this, psp);
  }

  return modem_set_psp_operation_.get();
}

ModemOperation* Sim7070AtModem::PowerOff() {
  if (!modem_poweroff_operation_ || modem_poweroff_operation_->is_finished()) {
    modem_poweroff_operation_ =
        std::make_unique<sim7070_modem_internal::ModemPowerOffOperation>(
            ae_context_, *this);
  }
  return modem_poweroff_operation_.get();
}

// ============================private members=============================== //
void Sim7070AtModem::SetupPoll() {
  poll_listener_.emplace(at_support_.dispatcher(),
                         "+CADATAIND: ", [this](auto&, auto pos) {
                           std::int32_t cid{};
                           at_support::ParseResponse(*pos, "+CADATAIND", cid);
                           PollEvent(cid);
                         });
}

void Sim7070AtModem::PollEvent(std::int32_t handle) {
  auto it = connections_.find(static_cast<ConnectionIndex>(handle));
  if (it == std::end(connections_)) {
    AE_TELED_ERROR("Poll unknown handle {}", handle);
    return;
  }

  if (recv_in_queue_ > 0) {
    return;
  }
  recv_in_queue_++;
  auto finally = [this]() noexcept {
    return ex::then([this]() noexcept { recv_in_queue_--; }) |
           ex::upon_error([this]() noexcept { recv_in_queue_--; });
  };

  operation_queue_.Push(at::Stage(ae_context_, [this, connection{*it},
                                                finally]() {
    auto conn = static_cast<std::int32_t>(connection);
    return ex::just() |
           at::MakeRequest(
               at_support_, "AT+CARECV=" + std::to_string(conn) + ",1024",
               at::Wait{
                   "+CARECV: ",
                   [this, connection](auto& at_buffer, auto pos) {
                     std::size_t size{};
                     auto parse_res =
                         at_support::ParseResponse(*pos, "+CARECV", size);
                     if (!parse_res) {
                       AE_TELED_ERROR("Failed to parse CARECV response");
                       return false;
                     }
                     AE_TELED_DEBUG("Received data size {} connection {}", size,
                                    static_cast<std::int32_t>(connection));

                     auto recv_data =
                         at_buffer.GetCrate(size, *parse_res + 1, pos);
                     AE_TELED_DEBUG("Received size {} bytes", recv_data.size());

                     if (recv_data.size() != size) {
                       AE_TELED_ERROR("Received {} bytes, expected {}",
                                      recv_data.size(), size);
                       return false;
                     }

                     data_event_.Emit(connection, recv_data);
                     return true;
                   }}) |
           ex::with_timeout(ae_context_, 15s) |
           ex::upon_error(Override{
               [&](ex::TimeoutError) noexcept {
                 AE_TELED_ERROR("Recv timeout");
               },
               [&](std::exception_ptr const&) noexcept {
                 AE_TELED_ERROR("Recv exception");
               },
               [&](auto err) noexcept { AE_TELED_ERROR("Recv error {}", err); },
           }) |
           finally();
  }));
}

} /* namespace ae */
#endif
