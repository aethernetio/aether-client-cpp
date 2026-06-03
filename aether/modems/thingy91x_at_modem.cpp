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

#  include "aether/misc/override.h"
#  include "aether/misc/from_chars.h"
#  include "aether/executors/executors.h"
#  include "aether/serial_ports/at_support/at_stage.h"
#  include "aether/serial_ports/serial_port_factory.h"
#  include "aether/serial_ports/at_support/at_request.h"

#  include "aether/modems/modems_tele.h"

namespace ae {
static constexpr auto kWaitOk = at::Wait{"OK"};

namespace thingy91x_modem_internal {
OpenNetworkOperationImpl::OpenNetworkOperationImpl(AeContext const& ae_context,
                                                   Thingy91xAtModem& self,
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
  auto socket_type = (protocol_ == Protocol::kTcp) ? 1 : 2;
  return ex::just() |
         at::MakeRequest(at_support_,
                         "AT#XSOCKET=1," + std::to_string(socket_type) + ",0",
                         at::Wait{"#XSOCKET: ",
                                  [this](auto&, auto pos) {
                                    return at_support::ParseResponse(
                                               *pos, "#XSOCKET:", handle_)
                                        .has_value();
                                  }}) |
         ex::with_timeout(ae_context_, 2s) |
         at::MakeRequest(at_support_,
                         "AT#XSOCKETSELECT=" + std::to_string(handle_),
                         kWaitOk) |
         ex::with_timeout(ae_context_, 1s) |
         at::MakeRequest(at_support_, "AT#XSOCKETOPT=1,20,30", kWaitOk) |
         ex::with_timeout(ae_context_, 1s) |
         at::MakeRequest(
             at_support_,
             "AT#XCONNECT=\"" + host_ + "\"," + std::to_string(port_),
             kWaitOk) |
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
    AeContext const& ae_context, Thingy91xAtModem& self,
    ConnectionIndex connect_index)
    : ae_context_{ae_context},
      self_{&self},
      at_support_{self.at_support_},
      connect_index_{connect_index} {
  AE_TELED_DEBUG("Close network connection {}", connect_index_);
  RunPipeline();
}

auto CloseNetworkOperationImpl::Pipeline() {
  return ex::just() |
         at::MakeRequest(
             at_support_,
             "AT#XSOCKETSELECT=" +
                 std::to_string(static_cast<std::int32_t>(connect_index_)),
             kWaitOk) |
         ex::with_timeout(ae_context_, 1s) |
         at::MakeRequest(at_support_, "AT#XSOCKET=0", at::Wait{"OK"}) |
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
                                       Thingy91xAtModem& self,
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

  return ex::just() |
         at::MakeRequest(at_support_,
                         "AT#XSOCKETSELECT=" + std::to_string(handle),
                         kWaitOk) |
         at::MakeRequest(at_support_, "AT#XSEND", kWaitOk) |
         at::MakeRequest(
             at_support_,
             [this]() noexcept -> Result<std::size_t, int> {
               self_->serial_->Write(data_);

               constexpr std::string_view terminate = "+++";
               self_->serial_->Write(std::span<std::uint8_t const>{
                   reinterpret_cast<std::uint8_t const*>(terminate.data()),
                   terminate.size()});

               return Ok{data_.size() + terminate.size()};
             },
             at::Wait{"#XDATAMODE:",
                      [](auto&, auto pos) noexcept -> bool {
                        int code{-1};
                        auto res =
                            at_support::ParseResponse(*pos, "#XDATAMODE", code);
                        if (!res.has_value()) {
                          return false;
                        }
                        AE_TELED_DEBUG("Send data result code: {}", code);
                        return code == 0;
                      }}) |
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

class ModemStartedAlreadyOperation final : public ModemOperation {
 public:
  explicit ModemStartedAlreadyOperation() { SetResult(Ok{kIgnore}); }
};

class ModemStartOperation final : public ModemOperation {
 public:
  ModemStartOperation(AeContext const& ae_context, Thingy91xAtModem& self)
      : ae_context_{ae_context}, self_{&self}, at_support_{self.at_support_} {
    RunPipeline();
  }

 private:
  auto SetNetMode(kModemMode const modem_mode) {
    return ex::let_value([&, modem_mode]() noexcept {
      return ex::create<ex::set_value_t(std::string),
                        ex::set_error_t(int)>([&](auto& ctx) noexcept {
               auto cmd = std::invoke([&]() -> std::string {
                 switch (modem_mode) {
                   case kModemMode::kModeAuto:
                     return "AT%XSYSTEMMODE=1,1,0,4";  // Set modem mode Auto
                   case kModemMode::kModeCatM:
                     return "AT%XSYSTEMMODE=1,0,0,1";  // Set modem mode CatM
                   case kModemMode::kModeNbIot:
                     return "AT%XSYSTEMMODE=0,1,0,2";  // Set modem mode NbIot
                   case kModemMode::kModeCatMNbIot:
                     return "AT%XSYSTEMMODE=1,1,0,0";  // Set modem mode
                                                       // CatMNbIot
                   default:
                     return {};
                 }
               });

               if (!cmd.empty()) {
                 ex::set_value(std::move(ctx.receiver), std::move(cmd));
               } else {
                 ex::set_error(std::move(ctx.receiver), 1);
               }
             }) |
             ex::let_value([&](std::string& cmd) noexcept {
               return ex::just() |
                      at::MakeRequest(at_support_, std::move(cmd), kWaitOk);
             }) |
             ex::with_timeout(ae_context_, 1s);
    });
  }

  auto SetupNetwork(std::string const& operator_name,
                    std::string const& operator_code,
                    std::string const& apn_name, std::string const& apn_user,
                    std::string const& apn_pass, kModemMode modem_mode,
                    kAuthType auth_type) {
    AE_TELED_DEBUG("\n apn_user {}\n apn_pass {}\n auth_type {}", apn_user,
                   apn_pass, auth_type);

    std::string mode_str = std::invoke([&] {
      switch (modem_mode) {
        case kModemMode::kModeAuto:
          return "0";  // Set modem mode Auto
        case kModemMode::kModeCatM:
          return "8";  // Set modem mode CatM
        case kModemMode::kModeNbIot:
          return "9";  // Set modem mode NbIot
        default:
          return "0";
      }
    });

    std::string cmd = std::invoke([&] {
      // Connect to the network
      if (!operator_name.empty()) {
        // Operator long name
        return "AT+COPS=1,0,\"" + operator_name + "\"," + mode_str;
      } else if (!operator_code.empty()) {
        // Operator code
        return "AT+COPS=1,2,\"" + operator_code + "\"," + mode_str;
      } else {
        // Auto
        return std::string{"AT+COPS=0"};
      }
    });

    return at::MakeRequest(at_support_, std::move(cmd), at::Wait{"OK"}) |
           ex::with_timeout(ae_context_, std::chrono::seconds{120}) |
           at::MakeRequest(at_support_,
                           R"(AT+CGDCONT=0,"IP",")" + apn_name + "\"",
                           kWaitOk) |
           ex::with_timeout(ae_context_, 1s) |
           at::MakeRequest(at_support_,
                           R"(AT+CEREG=1,"IP",")" + apn_name + "\"", kWaitOk) |
           ex::with_timeout(ae_context_, 1s);
  }

  auto CheckSimStatus() {
    return at::MakeRequest(at_support_, "AT+CPIN?", kWaitOk) |
           ex::with_timeout(ae_context_, 1s);
  }

  auto SetupSim(std::uint16_t pin) {
    return ex::let_value([&, pin]() noexcept {
      auto pin_string = at_support::PinToString(pin);

      auto make_request = [&]() noexcept {
        return ex::just() |
               at::MakeRequest(at_support_, "AT+CPIN=" + pin_string, kWaitOk) |
               ex::with_timeout(ae_context_, 1s);
      };

      using res = ex::variant_sender<
          std::invoke_result_t<decltype(ex::just_error), int>,
          std::invoke_result_t<decltype(make_request)>>;

      if (!pin_string.empty()) {
        return res{make_request()};
      } else {
        AE_TELED_ERROR("Pin wrong!");
        return res{ex::just_error(1)};
      }
    });
  }

  auto Pipeline() {
    return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
               [&](auto& ctx) noexcept {
                 // Modem must be initiated
                 if (self_->initiated_) {
                   ex::set_value(std::move(ctx.receiver));
                 } else {
                   AE_TELED_ERROR("Modem not initiated");
                   ex::set_error(std::move(ctx.receiver), -1);
                 }
               }) |
           // Disabling full functionality
           at::MakeRequest(at_support_, "AT+CFUN=0", kWaitOk) |
           ex::with_timeout(ae_context_, 1s) |
           SetNetMode(kModemMode::kModeCatMNbIot) |
           SetupNetwork(modem_init_.operator_name, modem_init_.operator_code,
                        modem_init_.apn_name, modem_init_.apn_user,
                        modem_init_.apn_pass, modem_init_.modem_mode,
                        modem_init_.auth_type) |
           // Enabling full functionality and waiting for network
           // registration
           at::MakeRequest(at_support_, "AT+CFUN=1", kWaitOk,
                           at::Wait{"+CEREG: 2"}, at::Wait{"+CEREG: 1"}) |
           ex::with_timeout(ae_context_, 60s) | CheckSimStatus() |
           ex::let_value([&]() noexcept {
             auto setup_sim = [this]() noexcept {
               return ex::just() | SetupSim(modem_init_.use_pin);
             };
             using res =
                 ex::variant_sender<std::invoke_result_t<decltype(ex::just)>,
                                    std::invoke_result_t<decltype(setup_sim)>>;

             if (modem_init_.use_pin) {
               return res{setup_sim()};
             }
             // ignore setup sim
             return res{ex::just()};
           });
  }

  void RunPipeline() {
    // all AT operations must be in operation_queue_
    self_->operation_queue_.Push(at::Stage(ae_context_, [this]() noexcept {
      return Pipeline() | ex::then([&]() noexcept {
               self_->started_ = true;
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

  AeContext ae_context_;
  Thingy91xAtModem* self_;

  ModemInit modem_init_;
  AtSupport& at_support_;
};

class ModemStoppedAlreadyOperation final : public ModemOperation {
 public:
  explicit ModemStoppedAlreadyOperation() { SetResult(Ok{kIgnore}); }
};

class ModemStopOperation final : public ModemOperation {
 public:
  ModemStopOperation(AeContext const& ae_context, Thingy91xAtModem& self,
                     std::uint8_t res_mode)
      : ae_context_{ae_context},
        self_{&self},
        at_support_{self_->at_support_},
        res_mode_{res_mode} {
    RunPipeline();
  }

 private:
  auto Pipeline() {
    return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
               [&](auto& ctx) noexcept {
                 // Modem must be started
                 if (!self_->started_) {
                   ex::set_error(std::move(ctx.receiver), -1);
                   return;
                 }
                 // Valid reset modes: 0 (hard) or 1 (soft)
                 if (res_mode_ >= 2) {
                   ex::set_error(std::move(ctx.receiver), -2);
                   return;
                 }
                 ex::set_value(std::move(ctx.receiver));
               }) |
           // Disabling full functionality
           at::MakeRequest(at_support_, "AT+CFUN=0", kWaitOk) |
           ex::with_timeout(ae_context_, 1s) |
           /**
            * Performs a factory reset of the modem using the specified reset
            * mode.
            * This function sends an AT%XFACTORYRESET command with the
            * specified mode parameter to perform a factory reset operation on
            * the modem. The command execution is verified by checking for "OK"
            * response within a 2-second timeout.
            *
            * @param[in] mode Reset operation mode. Valid values are:
            *                 - 1: Soft reset (preserves some settings)
            *                 - 0: Hard reset (full factory defaults)
            *
            * @return kModemError indicating operation status:
            *         - kModemError::kNoError: Reset command executed
            * successfully
            *         - kModemError::kResetMode: Invalid mode parameter (?2)
            *         - Other kModemError codes: Communication error or missing
            * "OK" response
            *
            * @note
            * - The function will return kResetMode error immediately for
            * invalid modes (?2)
            * - Actual modem behavior depends on modem firmware implementation
            * - Hard reset (mode 1) will erase all user configurations
            * - Device may reboot after successful execution
            * - Uses 2000ms response timeout for command verification
            */
           at::MakeRequest(at_support_,
                           "AT%XFACTORYRESET=" + std::to_string(res_mode_),
                           kWaitOk) |
           ex::with_timeout(ae_context_, 2s);
  }

  void RunPipeline() {
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

  AeContext ae_context_;
  Thingy91xAtModem* self_;
  AtSupport& at_support_;
  std::uint8_t res_mode_;
};

class ModemSetPowerSaveParamOperation final : public ModemOperation {
 public:
  ModemSetPowerSaveParamOperation(AeContext const& ae_context,
                                  Thingy91xAtModem& self,
                                  ModemPowerSaveParam psp)
      : ae_context_{ae_context},
        self_{&self},
        at_support_{self.at_support_},
        psp_{std::move(psp)} {
    RunPipeline();
  }

 private:
  auto SetEdrx(EdrxMode const edrx_mode, EdrxActTType const act_type,
               kEDrx const edrx_val) {
    std::string req_edrx_str =
        std::bitset<4>(edrx_val.bits.ReqEDRXValue).to_string();
    std::string ptw_str = std::bitset<4>(edrx_val.bits.PTWValue).to_string();

    std::string cmd =
        "AT+CEDRXS=" + std::to_string(static_cast<std::uint8_t>(edrx_mode)) +
        "," + std::to_string(static_cast<std::uint8_t>(act_type)) + ",\"" +
        req_edrx_str + "\"";

    return at::MakeRequest(at_support_, std::move(cmd), kWaitOk) |
           ex::with_timeout(ae_context_, 2s) |
           at::MakeRequest(
               at_support_,
               "AT%XPTW=" +
                   std::to_string(static_cast<std::uint8_t>(act_type)) + ",\"" +
                   ptw_str + "\"",
               kWaitOk) |
           ex::with_timeout(ae_context_, 2s);
  }

  auto SetRai(std::uint8_t const rai_mode) {
    return ex::let_value([this, rai_mode]() noexcept {
      return ex::create<ex::set_value_t(std::string), ex::set_error_t(int)>(
                 [&](auto& ctx) noexcept {
                   if (rai_mode >= 3) {
                     ex::set_error(std::move(ctx.receiver), -1);
                     return;
                   }
                   ex::set_value(std::move(ctx.receiver),
                                 "AT%RAI=" + std::to_string(rai_mode));
                 }) |
             ex::let_value([this](std::string& cmd) noexcept {
               return at::MakeRequest(ex::just(), at_support_, std::move(cmd),
                                      kWaitOk) |
                      ex::with_timeout(ae_context_, 2s);
             });
    });
  }

  auto SetBandLock(std::uint8_t const bl_mode,
                   const std::vector<std::int32_t>& bands) {
    return ex::let_value([this, bl_mode, bands]() noexcept {
      return ex::create<ex::set_value_t(std::string), ex::set_error_t(int)>(
                 [&](auto& ctx) noexcept {
                   if (bl_mode >= 4) {
                     ex::set_error(std::move(ctx.receiver), -1);
                     return;
                   }
                   std::string cmd = "AT%XBANDLOCK=" + std::to_string(bl_mode);
                   if (bl_mode == 1 && !bands.empty()) {
                     std::uint64_t band_bit1{0}, band_bit2{0};
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
                   ex::set_value(std::move(ctx.receiver), std::move(cmd));
                 }) |
             ex::let_value([this](std::string& cmd) noexcept {
               return at::MakeRequest(ex::just(), at_support_, std::move(cmd),
                                      kWaitOk) |
                      ex::with_timeout(ae_context_, 2s);
             });
    });
  }

  auto SetTxPower(kModemMode const modem_mode,
                  std::vector<BandPower> const& power) {
    return ex::let_value([this, modem_mode, &power]() noexcept {
      return ex::create<ex::set_value_t(std::string), ex::set_error_t(int)>(
                 [&](auto& ctx) noexcept {
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
                     ex::set_error(std::move(ctx.receiver), 1);
                     return;
                   }
                   if (power.empty()) {
                     ex::set_error(std::move(ctx.receiver), 2);
                     return;
                   }
                   for (auto& pwr : power) {
                     if (pwr.band == kModemBand::kALL_BAND) {
                       all_bands = true;
                       all_bands_power = static_cast<std::int8_t>(pwr.power);
                     } else {
                       k++;
                       bands +=
                           "," +
                           std::to_string(static_cast<std::int8_t>(pwr.band)) +
                           "," +
                           std::to_string(static_cast<std::int8_t>(pwr.power));
                     }
                   }
                   if (all_bands) {
                     cmd += std::to_string(system_mode) + ",0," +
                            std::to_string(all_bands_power);
                   } else {
                     cmd += std::to_string(system_mode) + "," +
                            std::to_string(k) + "," + bands;
                   }
                   ex::set_value(std::move(ctx.receiver), std::move(cmd));
                 }) |
             ex::let_value([this](std::string& cmd) noexcept {
               return at::MakeRequest(ex::just(), at_support_, std::move(cmd),
                                      kWaitOk) |
                      ex::with_timeout(ae_context_, 1s);
             });
    });
  }

  auto Pipeline() {
    return at::MakeRequest(ex::just(), at_support_, "AT+CFUN=0", kWaitOk) |
           ex::with_timeout(ae_context_, 1s) |
           SetEdrx(psp_.edrx_mode, psp_.act_type, psp_.edrx_val) |
           SetRai(psp_.rai_mode) | SetBandLock(psp_.bands_mode, psp_.bands) |
           SetTxPower(psp_.modem_mode, psp_.power) |
           at::MakeRequest(at_support_, "AT+CFUN=1", kWaitOk) |
           ex::with_timeout(ae_context_, 1s);
  }

  void RunPipeline() {
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

  AeContext ae_context_;
  Thingy91xAtModem* self_;
  AtSupport& at_support_;
  ModemPowerSaveParam psp_;
};

class ModemPowerOffOperation final : public ModemOperation {
 public:
  ModemPowerOffOperation(AeContext const& ae_context, Thingy91xAtModem& self)
      : ae_context_{ae_context}, self_{&self}, at_support_{self.at_support_} {
    RunPipeline();
  }

 private:
  auto Pipeline() {
    return ex::just() | at::MakeRequest(at_support_, "AT+CFUN=0", kWaitOk) |
           ex::with_timeout(ae_context_, 1s);
  }

  void RunPipeline() {
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

  AeContext ae_context_;
  Thingy91xAtModem* self_;
  AtSupport& at_support_;
};
}  // namespace thingy91x_modem_internal

Thingy91xAtModem::Thingy91xAtModem(AeContext const& ae_context,
                                   IPoller::ptr const& poller,
                                   ModemInit modem_init)
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
  AE_TELED_DEBUG("Thingy91xAtModem init");
  Init();
}

void Thingy91xAtModem::Init() {
  operation_queue_.Push(at::Stage(ae_context_, [this]() {
    return at::MakeRequest(ex::just(), at_support_, "AT", kWaitOk) |
           at::MakeRequest(at_support_, "AT+CMEE=1", kWaitOk) |
           ex::with_timeout(ae_context_, 1s) | ex::then([&]() noexcept {
             AE_TELED_INFO("Thingy91xAtModem init success");
             initiated_ = true;
           }) |
           ex::upon_error(Override{
               [](ex::TimeoutError) {
                 AE_TELED_ERROR("Thingy91xAtModem init failed, with timeout");
               },
               [](int error) {
                 AE_TELED_ERROR("Thingy91xAtModem init failed, with error {}",
                                error);
               }});
  }));
}

ModemOperation* Thingy91xAtModem::Start() {
  if (!modem_start_operation_ || modem_start_operation_->is_finished()) {
    if (started_) {
      modem_start_operation_ = std::make_unique<
          thingy91x_modem_internal::ModemStartedAlreadyOperation>();
    } else {
      modem_start_operation_ =
          std::make_unique<thingy91x_modem_internal::ModemStartOperation>(
              ae_context_, *this);
    }
  }

  return modem_start_operation_.get();
}

ModemOperation* Thingy91xAtModem::Stop() {
  if (!modem_stop_operation_ || modem_stop_operation_->is_finished()) {
    if (!started_) {
      modem_stop_operation_ = std::make_unique<
          thingy91x_modem_internal::ModemStoppedAlreadyOperation>();
    } else {
      modem_stop_operation_ =
          std::make_unique<thingy91x_modem_internal::ModemStopOperation>(
              ae_context_, *this, 1);
    }
  }

  return modem_stop_operation_.get();
}

OpenNetworkOperation* Thingy91xAtModem::OpenNetwork(Protocol protocol,
                                                    std::string const& host,
                                                    std::uint16_t port) {
  // setup polling on demund
  if (!poll_listener_) {
    SetupPoll();
  }

  auto* op =
      open_network_pool_.Create(ae_context_, *this, protocol, host, port);
  return op;
}

ModemOperation* Thingy91xAtModem::CloseNetwork(ConnectionIndex connect_index) {
  auto* op = close_network_pool_.Create(ae_context_, *this, connect_index);
  return op;
}

WriteOperation* Thingy91xAtModem::WritePacket(
    ConnectionIndex connect_index, std::span<std::uint8_t const> data) {
  if (data.size() > kModemMTU) {
    assert(false);
    return nullptr;
  }

  auto* op = write_pool_.Create(ae_context_, *this, connect_index, data);
  return op;
}

Thingy91xAtModem::DataEvent::Subscriber Thingy91xAtModem::data_event() {
  return EventSubscriber{data_event_};
}

ModemOperation* Thingy91xAtModem::SetPowerSaveParam(
    ModemPowerSaveParam const& psp) {
  if (!modem_set_psp_operation_ || modem_set_psp_operation_->is_finished()) {
    modem_set_psp_operation_ = std::make_unique<
        thingy91x_modem_internal::ModemSetPowerSaveParamOperation>(ae_context_,
                                                                   *this, psp);
  }
  return modem_set_psp_operation_.get();
}

ModemOperation* Thingy91xAtModem::PowerOff() {
  if (!modem_poweroff_operation_ || modem_poweroff_operation_->is_finished()) {
    modem_poweroff_operation_ =
        std::make_unique<thingy91x_modem_internal::ModemPowerOffOperation>(
            ae_context_, *this);
  }
  return modem_poweroff_operation_.get();
}

// ============================private members=============================== //

void Thingy91xAtModem::SetupPoll() {
  poll_listener_.emplace(
      at_support_.dispatcher(), "#XPOLL: ", [this](auto&, auto pos) {
        // read event flags from POLL answer
        std::int32_t handle{};
        std::string flags;
        auto res = at_support::ParseResponse(*pos, "#XPOLL", handle, flags);
        if (!res) {
          AE_TELED_ERROR("Failed to parse XPOLL response");
          return;
        }
        PollEvent(handle, flags);
      });

  poll_task_.emplace(
      ae_context_,
      [this]() noexcept {
        if (connections_.empty()) {
          return;
        }
        if ((poll_in_queue_ > 0) || (recv_in_queue_ > 0)) {
          return;
        }
        poll_in_queue_++;
        auto finally = [&]() noexcept {
          return ex::then([&]() noexcept { poll_in_queue_--; }) |
                 ex::upon_error([&](auto&&...) noexcept { poll_in_queue_--; });
        };

        operation_queue_.Push(at::Stage(ae_context_, [this, finally]() {
          // poll all connections
          // the result must be #XPOLL: answer we subscribed earlier to
          std::string handles;
          for (auto ci : connections_) {
            handles += "," + std::to_string(ci);
          }
          return ex::just() |
                 at::MakeRequest(at_support_, "AT#XPOLL=0" + handles, kWaitOk) |
                 ex::with_timeout(ae_context_, 1s) | finally();

          // TODO: maybe it's possible to setup next poll in finally?
        }));
      },
      200ms);
}

void Thingy91xAtModem::PollEvent(std::int32_t handle, std::string_view flags) {
  // get connection index
  auto it = connections_.find(static_cast<ConnectionIndex>(handle));
  if (it == std::end(connections_)) {
    AE_TELED_ERROR("Poll unknown handle {}", handle);
    return;
  }

  // flags is in hexadecimal format like "0x001"
  auto flags_val = FromChars<std::uint32_t>(flags, 16);
  if (!flags_val) {
    return;
  }
  constexpr std::uint32_t kPollIn = 0x01;
  if ((*flags_val & kPollIn) != 0) {
    if (recv_in_queue_ > 0) {
      return;
    }
    recv_in_queue_++;
    auto finally = [this]() noexcept {
      return ex::then([this]() noexcept { recv_in_queue_--; }) |
             ex::upon_error([this]() noexcept { recv_in_queue_--; });
    };

    operation_queue_.Push(
        at::Stage(ae_context_, [this, connection{*it}, finally]() {
          auto handle = static_cast<std::int32_t>(connection);
          return ex::just() |
                 at::MakeRequest(at_support_,
                                 "AT#XSOCKETSELECT=" + std::to_string(handle),
                                 kWaitOk) |
                 ex::with_timeout(ae_context_, 5s) |
                 at::MakeRequest(
                     at_support_, "AT#XRECV=0,64",
                     at::Wait{"#XRECV:",
                              [this, connection](auto& at_buffer, auto pos) {
                                std::size_t size{};
                                auto parse_end = at_support::ParseResponse(
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

                                data_event_.Emit(connection, data_crate);
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
                     [&](auto err) noexcept {
                       AE_TELED_ERROR("Recv error {}", err);
                     },
                 }) |
                 finally();
        }));
  }
}

} /* namespace ae */
#endif
