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

/**
 * @file sim7070_at_modem.cpp
 * @brief SIM7070 startup retries, socket I/O, and orderly modem shutdown.
 */

#include "aether/modems/sim7070_at_modem.h"
#if AE_SUPPORT_MODEMS && AE_ENABLE_SIM7070

#  include <cassert>
#  include <chrono>
#  include <limits>
#  include <string_view>
#  include <utility>

#  include "aether-miscpp/misc/override.h"
#  include "aether/clock.h"
#  include "aether/executors/executors.h"
#  include "aether/serial_ports/at_support/at_request.h"
#  include "aether/serial_ports/at_support/at_stage.h"
#  include "aether/serial_ports/serial_port_factory.h"

#  include "aether/modems/esp32_modem_boot.h"
#  include "aether/modems/modems_tele.h"

namespace ae {
using namespace std::chrono_literals;

static const auto kWaitOk = at::Wait{"OK"};

namespace sim7070_modem_internal {
using CleanupSender = ex::AnySender<ex::set_value_t(), ex::set_error_t(int),
                                    ex::set_error_t(ex::TimeoutError)>;

/**
 * @brief Query SIM readiness with delayed retries after AT command errors.
 * @param context Runtime context that must outlive the sender operation.
 * @param at_support Dispatcher used for the AT requests.
 * @param retries Number of retries allowed after the first failed request.
 * @return A sender bounded by a 15-second timeout, including retry delays.
 */
CleanupSender WaitForSim(AeContext const& context, AtSupport& at_support,
                         int retries) {
  return CleanupSender{
      at::MakeRequest(ex::just(), at_support, "AT+CPIN?", kWaitOk,
                      at::Wait{"+CPIN:"}) |
      ex::let_error([&context, &at_support, retries](int error) {
        if (retries == 0) {
          return CleanupSender{ex::just_error(error)};
        }
        // After CFUN=1 the modem can acknowledge the command before its SIM
        // interface is ready. Retry the query without blocking the scheduler.
        auto pause = ex::create<ex::set_value_t()>(
            [&context, task = TaskSubscription{}](auto& ctx) mutable noexcept {
              task = context.scheduler().DelayedTask(
                  [&ctx]() noexcept { ex::set_value(std::move(ctx.receiver)); },
                  Now() + 500ms);
              if (!task) {
                AE_TELED_ERROR("Failed to schedule SIM readiness check");
                assert(false && "Task allocation failed");
              }
            });
        return CleanupSender{
            std::move(pause) |
            ex::let_value([&context, &at_support, retries]() noexcept {
              return WaitForSim(context, at_support, retries - 1);
            })};
      }) |
      ex::with_timeout(context, 15s)};
}

/**
 * @brief Retry operator selection while the modem finishes waking up.
 * @param context Runtime context that must outlive the sender operation.
 * @param at_support Dispatcher used for the AT requests.
 * @param command Complete operator-selection AT command.
 * @param retries Number of retries allowed after the first failed request.
 * @return A sender bounded by a 180-second timeout, including retry delays.
 */
CleanupSender SelectOperator(AeContext const& context, AtSupport& at_support,
                             std::string command, int retries) {
  return CleanupSender{
      at::MakeRequest(ex::just(), at_support, command, kWaitOk) |
      ex::let_error([&context, &at_support, command, retries](int error) {
        if (retries == 0) {
          return CleanupSender{ex::just_error(error)};
        }
        // Operator selection can still fail transiently after CPIN is ready.
        auto pause = ex::create<ex::set_value_t()>(
            [&context, task = TaskSubscription{}](auto& ctx) mutable noexcept {
              task = context.scheduler().DelayedTask(
                  [&ctx]() noexcept { ex::set_value(std::move(ctx.receiver)); },
                  Now() + 1s);
              if (!task) {
                AE_TELED_ERROR("Failed to schedule operator selection retry");
                assert(false && "Task allocation failed");
              }
            });
        return CleanupSender{
            std::move(pause) |
            ex::let_value([&context, &at_support, command, retries]() noexcept {
              return SelectOperator(context, at_support, command, retries - 1);
            })};
      }) |
      ex::with_timeout(context, 180s)};
}

/**
 * @brief Close stale connections sequentially before completing startup.
 * @param ae_context Runtime context that must outlive the sender operation.
 * @param at_support Dispatcher used for the AT requests.
 * @param stale_connections Connection set consumed by the cleanup sequence.
 * @return A sender that completes when cleanup ends or a request fails.
 */
CleanupSender CleanupConnections(AeContext const& ae_context,
                                 AtSupport& at_support,
                                 std::set<ConnectionIndex>& stale_connections) {
  if (stale_connections.empty()) {
    return CleanupSender{ex::just()};
  }

  auto connection = *stale_connections.begin();
  stale_connections.erase(stale_connections.begin());
  AE_TELED_DEBUG("Close stale modem connection {}", connection);

  return CleanupSender{
      at::MakeRequest(
          ex::just(), at_support,
          "AT+CACLOSE=" + std::to_string(static_cast<int>(connection)),
          kWaitOk) |
      ex::with_timeout(ae_context, 10s) |
      ex::let_value([&ae_context, &at_support, &stale_connections]() noexcept {
        return CleanupConnections(ae_context, at_support, stale_connections);
      })};
}

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
  static constexpr auto kMaxConnectionIndex = std::int32_t{12};
  for (auto index = std::int32_t{0}; index <= kMaxConnectionIndex; ++index) {
    auto connection = static_cast<ConnectionIndex>(index);
    if (!self.connections_.contains(connection) &&
        !self.opening_connections_.contains(connection)) {
      handle_ = index;
      self.opening_connections_.emplace(connection);
      break;
    }
  }

  AE_TELED_DEBUG("Open {} connection for {}:{}",
                 protocol == Protocol::kTcp ? "tcp" : "udp", host_, port_);
  RunPipeline();
}

auto OpenNetworkOperationImpl::Pipeline() {
  using res = ex::AnySender<ex::set_value_t(), ex::set_error_t(int),
                            ex::set_error_t(ex::TimeoutError)>;
  if (handle_ < 0) {
    AE_TELED_ERROR("No free modem connection index");
    return res{ex::just_error(-6)};
  }

  // AT+CAOPEN=<cid>,<pdp_index>,<conn_type>,<server>,<port>[,<recv_mode>]
  auto protocol_str = [&]() -> std::string_view {
    if (protocol_ == Protocol::kTcp) {
      return "TCP";
    }
    if (protocol_ == Protocol::kUdp) {
      return "UDP";
    }
    return "UNKNOWN";
  }();

  return res{ex::just() |
             at::MakeRequest(at_support_,
                             Format(R"(AT+CAOPEN={},0,"{}","{}",{})", handle_,
                                    protocol_str, host_, port_),
                             at::Wait{Format("+CAOPEN: {},0", handle_),
                                      [this](AtBuffer&, auto pos) {
                                        std::int32_t response_handle{};
                                        std::int32_t result_code{};
                                        if (!at_support::ParseResponse(
                                                *pos, "+CAOPEN",
                                                response_handle, result_code) ||
                                            response_handle != handle_) {
                                          return false;
                                        }
                                        open_result_ = result_code;
                                        return true;
                                      }}) |
             ex::with_timeout(ae_context_, 10s)};
}

void OpenNetworkOperationImpl::RunPipeline() {
  self_->operation_queue_.Push(at::Stage(ae_context_, [this]() {
    return Pipeline() | ex::then([this]() noexcept {
             self_->opening_connections_.erase(
                 static_cast<ConnectionIndex>(handle_));
             if (open_result_ != 0) {
               AE_TELED_ERROR("Open connection {} failed, CAOPEN result {}",
                              handle_, open_result_);
               SetResult(Error{static_cast<ModemError>(open_result_)});
               return;
             }
             AE_TELED_DEBUG("Opened connection {}", handle_);
             self_->connections_.emplace(static_cast<ConnectionIndex>(handle_));
             SetResult(Ok{static_cast<ConnectionIndex>(handle_)});
           }) |
           ex::upon_error(Override{
               [&](ex::TimeoutError) noexcept {
                 self_->opening_connections_.erase(
                     static_cast<ConnectionIndex>(handle_));
                 AE_TELED_ERROR("Open connection {} timeout", handle_);
                 SetResult(Error{static_cast<ModemError>(-2)});
               },
               [&](std::exception_ptr) noexcept {
                 self_->opening_connections_.erase(
                     static_cast<ConnectionIndex>(handle_));
                 SetResult(Error{static_cast<ModemError>(-3)});
               },
               [&](auto err) noexcept {
                 self_->opening_connections_.erase(
                     static_cast<ConnectionIndex>(handle_));
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
             self_->recv_in_queue_.erase(connect_index_);
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

  auto registration = CleanupSender{
      at::MakeRequest(ex::just(), at_support_, "AT+CREG=1;+CGREG=1;+CEREG=1",
                      kWaitOk) |
      ex::with_timeout(ae_context_, 180s) |
      at::MakeRequest(at_support_, "AT+CEREG?", kWaitOk,
                      at::Wait{"+CEREG:",
                               [this](AtBuffer&, auto pos) {
                                 std::int32_t reporting_mode{};
                                 std::int32_t registration_status{};
                                 if (!at_support::ParseResponse(
                                         *pos, "+CEREG", reporting_mode,
                                         registration_status)) {
                                   return false;
                                 }
                                 network_registered_ =
                                     registration_status == 1 ||
                                     registration_status == 5;
                                 return true;
                               }}) |
      ex::with_timeout(ae_context_, 1s) |
      ex::let_value([this, cmd = std::move(cmd)]() mutable noexcept {
        using res = ex::AnySender<ex::set_value_t(), ex::set_error_t(int),
                                  ex::set_error_t(ex::TimeoutError)>;

        if (network_registered_) {
          AE_TELED_DEBUG("Modem is already registered in the network");
          return res{ex::just()};
        }
        return res{SelectOperator(ae_context_, at_support_, std::move(cmd), 5)};
      }) |
      ex::let_value([this]() noexcept {
        return at::MakeRequest(
                   ex::just(), at_support_, "AT+CEREG?", kWaitOk,
                   at::Wait{"+CEREG:",
                            [this](AtBuffer&, auto pos) {
                              std::int32_t reporting_mode{};
                              std::int32_t registration_status{};
                              if (!at_support::ParseResponse(
                                      *pos, "+CEREG", reporting_mode,
                                      registration_status)) {
                                return false;
                              }
                              network_registered_ = registration_status == 1 ||
                                                    registration_status == 5;
                              return true;
                            }}) |
               ex::with_timeout(ae_context_, 1s);
      }) |
      ex::let_value([this]() noexcept {
        auto success = []() noexcept { return ex::just(); };
        auto error = []() noexcept { return ex::just_error(-5); };
        using res = ex::variant_sender<std::invoke_result_t<decltype(success)>,
                                       std::invoke_result_t<decltype(error)>>;

        if (!network_registered_) {
          AE_TELED_ERROR("Modem is not registered in the network");
          return res{error()};
        }
        return res{success()};
      })};
  auto context = CleanupSender{
      ex::just() |
      at::MakeRequest(at_support_, R"(AT+CGDCONT=1,"IP",")" + apn_name + "\"",
                      kWaitOk) |
      ex::with_timeout(ae_context_, 180s) |
      at::MakeRequest(at_support_,
                      "AT+CNCFG=0,0,\"" + apn_name + "\",\"" + apn_user +
                          "\",\"" + apn_pass + "\"," + type,
                      kWaitOk) |
      ex::with_timeout(ae_context_, 180s) |
      at::MakeRequest(at_support_, "AT+CNACT?", kWaitOk,
                      at::Wait{"+CNACT: 0,",
                               [this](AtBuffer&, auto pos) {
                                 std::int32_t context{};
                                 std::int32_t status{};
                                 if (!at_support::ParseResponse(
                                         *pos, "+CNACT", context, status)) {
                                   return false;
                                 }
                                 pdp_active_ = status == 1;
                                 return true;
                               }}) |
      ex::with_timeout(ae_context_, 1s)};
  auto activation = CleanupSender{
      std::move(registration) |
      ex::let_value([context = std::move(context)]() mutable noexcept {
        return std::move(context);
      }) |
      ex::let_value([this]() noexcept {
        auto activate = [this]() noexcept {
          return at::MakeRequest(
              ex::just(), at_support_,
              "AT+CNACT=0,1",  // Activate the PDP context
              kWaitOk,
              at::Wait{"+APP PDP: 0,", [this](AtBuffer&, auto pos) {
                         auto const& response = *pos;
                         auto line = std::string_view{
                             reinterpret_cast<char const*>(response.data()),
                             response.size()};
                         pdp_active_ = line.find("+APP PDP: 0,ACTIVE") !=
                                       std::string_view::npos;
                         return true;
                       }});
        };
        using res = ex::AnySender<ex::set_value_t(), ex::set_error_t(int)>;

        if (pdp_active_) {
          AE_TELED_DEBUG("PDP context is already active");
          return res{ex::just()};
        }
        return res{activate()};
      }) |
      ex::with_timeout(ae_context_, 180s)};
  return std::move(activation) | ex::let_value([this]() noexcept {
           stale_connections_.clear();
           socket_state_listener_.emplace(
               at_support_.dispatcher(),
               "+CASTATE:", [this](AtBuffer&, auto pos) {
                 std::int32_t connection{};
                 std::int32_t state{};
                 if (!at_support::ParseResponse(*pos, "+CASTATE", connection,
                                                state)) {
                   return;
                 }
                 if (state == 1 || state == 2) {
                   stale_connections_.emplace(
                       static_cast<ConnectionIndex>(connection));
                 }
               });
           return at::MakeRequest(ex::just(), at_support_, "AT+CASTATE?",
                                  kWaitOk) |
                  ex::with_timeout(ae_context_, 10s);
         }) |
         ex::let_value([this]() noexcept {
           socket_state_listener_.reset();
           return CleanupConnections(ae_context_, at_support_,
                                     stale_connections_);
         }) |
         ex::then([this]() noexcept {
           self_->connections_.clear();
           self_->opening_connections_.clear();
           self_->recv_in_queue_.clear();
         });
}

auto ModemStartOperation::CheckSimStatus() {
  return WaitForSim(ae_context_, at_support_, 20);
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
         ex::let_value([this]() noexcept {
           // Stop disables the SIM interface along with the radio. Restore
           // full functionality before querying the SIM on the next launch.
           return at::MakeRequest(ex::just(), at_support_, "AT+CFUN=1,0",
                                  kWaitOk) |
                  ex::with_timeout(ae_context_, 30s);
         }) |
         ex::let_value([this]() noexcept { return CheckSimStatus(); }) |
         ex::let_value([&]() noexcept {
           auto setup_sim = [this]() noexcept {
             return ex::just() | SetupSim(modem_init_.pin);
           };
           using res =
               ex::variant_sender<std::invoke_result_t<decltype(ex::just)>,
                                  std::invoke_result_t<decltype(setup_sim)>>;

           if (modem_init_.use_pin) {
             return res{setup_sim()};
           }
           return res{ex::just()};
         }) |
         SetBaudRate(modem_init_.serial_init.baud_rate) |
         SetNetMode(modem_init_.modem_mode) | ex::let_value([this]() noexcept {
           // Keep the large network setup type out of the preceding pipeline.
           return CleanupSender{SetupNetwork(
               modem_init_.operator_name, modem_init_.operator_code,
               modem_init_.apn_name, modem_init_.apn_user, modem_init_.apn_pass,
               modem_init_.modem_mode, modem_init_.auth_type)};
         });
}

void ModemStartOperation::RunPipeline() {
  // all AT operations must be in operation_queue_
  self_->operation_queue_.Push(at::Stage(ae_context_, [this]() noexcept {
    return Pipeline() | ex::then([&]() noexcept {
             if (!pdp_active_) {
               AE_TELED_ERROR("PDP context activation failed");
               SetResult(Error{static_cast<ModemError>(-4)});
               return;
             }
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

ex::AnySender<ex::set_value_t()> ModemStopOperation::CloseSockets() {
  if (remaining_.empty()) {
    return ex::AnySender<ex::set_value_t()>{ex::just()};
  }
  auto connection = *remaining_.begin();
  remaining_.erase(remaining_.begin());
  return ex::AnySender<ex::set_value_t()>{
      at::MakeRequest(ex::just(), at_support_,
                      "AT+CACLOSE=" + std::to_string(connection), kWaitOk) |
      ex::with_timeout(ae_context_, 10s) |
      ex::then([this, connection]() noexcept {
        self_->connections_.erase(connection);
      }) |
      ex::upon_error([this, connection](auto&&) noexcept {
        AE_TELED_ERROR("Sim7070 failed to close socket {} during shutdown",
                       connection);
        failed_ = true;
      }) |
      ex::let_value([this]() noexcept { return CloseSockets(); })};
}

auto ModemStopOperation::Pipeline() {
  using Sender = ex::AnySender<ex::set_value_t()>;
  // Erase intermediate sender types to keep MSVC template memory bounded.
  auto query =
      Sender{CloseSockets() | ex::let_value([this]() noexcept {
               return at::MakeRequest(
                          ex::just(), at_support_, "AT+CNACT?", kWaitOk,
                          at::Wait{"+CNACT: 0,",
                                   [this](AtBuffer&, auto pos) {
                                     std::int32_t context{};
                                     std::int32_t status{};
                                     if (!at_support::ParseResponse(
                                             *pos, "+CNACT", context, status)) {
                                       return false;
                                     }
                                     pdp_active_ = status != 0;
                                     return true;
                                   }}) |
                      ex::with_timeout(ae_context_, 1s) |
                      ex::upon_error([this](auto&&) noexcept {
                        // Unknown context state: still attempt deactivation.
                        AE_TELED_ERROR(
                            "Sim7070 failed to query context during shutdown");
                        failed_ = true;
                      });
             })};
  auto deactivate =
      Sender{std::move(query) | ex::let_value([this]() noexcept {
               if (!pdp_active_) {
                 return Sender{ex::just()};
               }
               // Install both waits before sending: the URC can precede OK.
               return Sender{
                   at::MakeRequest(ex::just(), at_support_, "AT+CNACT=0,0",
                                   kWaitOk, at::Wait{"+APP PDP: 0,DEACTIVE"}) |
                   ex::with_timeout(ae_context_, 10s) |
                   ex::upon_error([this](auto&&) noexcept {
                     AE_TELED_ERROR("Sim7070 context deactivation failed");
                     failed_ = true;
                   })};
             })};
  return std::move(deactivate) | ex::let_value([this]() noexcept {
           // Also detach LTE; this is a fallback if context deactivation
           // failed.
           return at::MakeRequest(ex::just(), at_support_, "AT+CFUN=0",
                                  kWaitOk) |
                  ex::with_timeout(ae_context_, 30s);
         });
}

void ModemStopOperation::RunPipeline() {
  self_->operation_queue_.Push(at::Stage(ae_context_, [this]() {
    remaining_ = self_->connections_;
    return Pipeline() |
           ex::then([this]() noexcept { self_->connections_.clear(); }) |
           ex::upon_error([this](auto&&) noexcept {
             AE_TELED_ERROR("Sim7070 network deactivation failed");
             failed_ = true;
           }) |
           ex::then([this]() noexcept {
             self_->started_ = false;
             // Stop is terminal: release the port even if the object graph
             // retains this driver, and also when deactivation failed.
             self_->serial_->Close();
             if (failed_) {
               SetResult(Error{static_cast<ModemError>(-1)});
               return;
             }
             SetResult(Ok{kIgnore});
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
    : Sim7070AtModem{ae_context, modem_init,
                     SerialPortFactory::CreatePort(ae_context, poller,
                                                   modem_init.serial_init)} {}

Sim7070AtModem::Sim7070AtModem(AeContext const& ae_context,
                               ModemInit modem_init,
                               std::unique_ptr<ISerialPort> serial)
    : ae_context_{ae_context},
      modem_init_{std::move(modem_init)},
      serial_{std::move(serial)},
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
#  if defined(ESP_PLATFORM) && defined(AE_MODEM_PWR_GPIO) && \
      defined(AE_MODEM_DTR_GPIO)
    auto ready =
        esp32_modem_boot_internal::EnsureReady(ae_context_, at_support_);
#  else
    auto ready = at::MakeRequest(ex::just(), at_support_, "AT", kWaitOk) |
                 ex::with_timeout(ae_context_, 1s);
#  endif
    return std::move(ready) | ex::let_value([this]() noexcept {
             return ex::just() | at::MakeRequest(at_support_, "ATE0", kWaitOk) |
                    at::MakeRequest(at_support_, "AT+CMEE=1", kWaitOk) |
                    ex::with_timeout(ae_context_, 1s);
           }) |
           ex::then([&]() noexcept {
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
               [](std::exception_ptr const&) noexcept {
                 AE_TELED_ERROR("Sim7070AtModem init failed, with exception");
               },
           });
  }));
}

ModemOperation* Sim7070AtModem::Start() {
  if (stopping_) {
    return nullptr;
  }
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
  if (!modem_stop_operation_) {
    stopping_ = true;
    poll_listener_.reset();
    buffer_full_listener_.reset();
    connection_closed_listener_.reset();
    modem_stop_operation_ =
        std::make_unique<sim7070_modem_internal::ModemStopOperation>(
            ae_context_, *this);
  }
  return modem_stop_operation_.get();
}
OpenNetworkOperation* Sim7070AtModem::OpenNetwork(Protocol protocol,
                                                  std::string const& host,
                                                  std::uint16_t port) {
  if (stopping_) {
    return nullptr;
  }
  // setup polling on demand
  if (!poll_listener_) {
    SetupPoll();
  }

  auto* op =
      open_network_pool_.Create(ae_context_, *this, protocol, host, port);
  return op;
}

ModemOperation* Sim7070AtModem::CloseNetwork(ConnectionIndex connect_index) {
  if (stopping_) {
    return modem_stop_operation_.get();
  }
  auto* op = close_network_pool_.Create(ae_context_, *this, connect_index);
  return op;
}

WriteOperation* Sim7070AtModem::WritePacket(
    ConnectionIndex connect_index, std::span<std::uint8_t const> data) {
  if (stopping_) {
    return nullptr;
  }
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
  if (stopping_) {
    return nullptr;
  }
  if (!modem_set_psp_operation_ || modem_set_psp_operation_->is_finished()) {
    modem_set_psp_operation_ = std::make_unique<
        sim7070_modem_internal::ModemSetPowerSaveParamOperation>(ae_context_,
                                                                 *this, psp);
  }

  return modem_set_psp_operation_.get();
}

ModemOperation* Sim7070AtModem::PowerOff() {
  if (stopping_) {
    return nullptr;
  }
  if (!modem_poweroff_operation_ || modem_poweroff_operation_->is_finished()) {
    modem_poweroff_operation_ =
        std::make_unique<sim7070_modem_internal::ModemPowerOffOperation>(
            ae_context_, *this);
  }
  return modem_poweroff_operation_.get();
}

// ============================private members=============================== //
void Sim7070AtModem::SetupPoll() {
  if (stopping_) {
    return;
  }
  connection_closed_listener_.emplace(
      at_support_.dispatcher(), "+CASTATE:", [this](auto&, auto pos) {
        std::int32_t cid{-1};
        std::int32_t state{-1};
        if (!at_support::ParseResponse(*pos, "+CASTATE", cid, state) ||
            cid < 0 || cid > std::numeric_limits<ConnectionIndex>::max() ||
            state != 0) {
          return;
        }
        auto connection = static_cast<ConnectionIndex>(cid);
        if (connections_.erase(connection) == 0) {
          return;
        }
        AE_TELED_ERROR("Modem connection {} closed", cid);
        NotifyConnectionClosed(connection);
      });
  poll_listener_.emplace(at_support_.dispatcher(),
                         "+CADATAIND: ", [this](auto&, auto pos) {
                           std::int32_t cid{};
                           at_support::ParseResponse(*pos, "+CADATAIND", cid);
                           PollEvent(cid);
                         });
  buffer_full_listener_.emplace(
      at_support_.dispatcher(), "+CAURC: buffer full,",
      [this](auto&, auto pos) {
        auto line = std::string_view{reinterpret_cast<char const*>(pos->data()),
                                     pos->size()};
        auto separator = line.find_last_of(',');
        if (separator == std::string_view::npos) {
          return;
        }
        auto cid = FromChars<std::int32_t>(line.substr(separator + 1));
        if (cid.has_value()) {
          PollEvent(*cid);
        }
      });
}

void Sim7070AtModem::PollEvent(std::int32_t handle) {
  if (stopping_) {
    return;
  }
  auto it = connections_.find(static_cast<ConnectionIndex>(handle));
  if (it == std::end(connections_)) {
    AE_TELED_ERROR("Poll unknown handle {}", handle);
    return;
  }

  auto connection = static_cast<ConnectionIndex>(handle);
  if (!recv_in_queue_.emplace(connection).second) {
    return;
  }
  auto finally = [this, connection]() noexcept {
    return ex::then([this, connection]() noexcept {
             recv_in_queue_.erase(connection);
           }) |
           ex::upon_error([this, connection]() noexcept {
             recv_in_queue_.erase(connection);
           });
  };

  operation_queue_.Push(at::Stage(ae_context_, [this, connection, finally]() {
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
