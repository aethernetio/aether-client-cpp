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

#ifndef AETHER_MODEMS_SIM7070_AT_MODEM_H_
#define AETHER_MODEMS_SIM7070_AT_MODEM_H_

#include "aether/config.h"

#if AE_SUPPORT_MODEMS && AE_ENABLE_SIM7070
#  include <set>
#  include <memory>

#  include "aether/ae_context.h"
#  include "aether/poller/poller.h"
#  include "aether/actions/action_pool.h"
#  include "aether/actions/actions_queue.h"
#  include "aether/actions/repeatable_task.h"
#  include "aether/serial_ports/iserial_port.h"
#  include "aether/serial_ports/at_support/at_support.h"

#  include "aether/modems/imodem_driver.h"

namespace ae {
class Sim7070AtModem;

namespace sim7070_modem_internal {
class OpenNetworkOperationImpl final : public OpenNetworkOperation {
 public:
  explicit OpenNetworkOperationImpl(AeContext const& ae_context,
                                    Sim7070AtModem& self, Protocol protocol,
                                    std::string host, std::uint16_t port);

 private:
  auto Pipeline();
  void RunPipeline();

  AeContext ae_context_;
  Sim7070AtModem* self_;
  AtSupport& at_support_;
  Protocol protocol_;
  std::string host_;
  std::uint16_t port_;
  std::int32_t handle_{-1};
};

class CloseNetworkOperationImpl final : public ModemOperation {
 public:
  explicit CloseNetworkOperationImpl(AeContext const& ae_context,
                                     Sim7070AtModem& self,
                                     ConnectionIndex connect_index);

 private:
  auto Pipeline();
  void RunPipeline();

  AeContext ae_context_;
  Sim7070AtModem* self_;
  AtSupport& at_support_;
  ConnectionIndex connect_index_;
};

class WriteOperationImpl final : public WriteOperation {
 public:
  explicit WriteOperationImpl(AeContext const& ae_context, Sim7070AtModem& self,
                              ConnectionIndex connect_index,
                              std::span<std::uint8_t const> data);

 private:
  auto Pipeline();
  void RunPipeline();

  AeContext ae_context_;
  Sim7070AtModem* self_;
  AtSupport& at_support_;
  ConnectionIndex connect_index_;
  std::span<std::uint8_t const> data_;
};

class ModemStartedAlreadyOperation final : public ModemOperation {
 public:
  explicit ModemStartedAlreadyOperation() { SetResult(Ok{kIgnore}); };
};

class ModemStartOperation final : public ModemOperation {
 public:
  explicit ModemStartOperation(AeContext const& ae_context,
                               Sim7070AtModem& self);

 private:
  auto SetBaudRate(kBaudRate const rate);
  auto SetNetMode(kModemMode const modem_mode);
  auto SetupNetwork(std::string const& operator_name,
                    std::string const& operator_code,
                    std::string const& apn_name, std::string const& apn_user,
                    std::string const& apn_pass, kModemMode modem_mode,
                    kAuthType auth_type);
  auto CheckSimStatus();
  auto SetupSim(std::uint16_t pin);
  auto SetupProtoPar();
  auto Pipeline();

  void RunPipeline();

  AeContext ae_context_;
  Sim7070AtModem* self_;
  ModemInit modem_init_;
  AtSupport& at_support_;
};

class ModemStoppedAlreadyOperation final : public ModemOperation {
 public:
  explicit ModemStoppedAlreadyOperation() { SetResult(Ok{kIgnore}); };
};

class ModemStopOperation final : public ModemOperation {
 public:
  explicit ModemStopOperation(AeContext const& ae_context,
                              Sim7070AtModem& self);

 private:
  auto Pipeline();
  void RunPipeline();

  AeContext ae_context_;
  Sim7070AtModem* self_;
  AtSupport& at_support_;
};

class ModemSetPowerSaveParamOperation final : public ModemOperation {
 public:
  explicit ModemSetPowerSaveParamOperation(AeContext const& ae_context,
                                           Sim7070AtModem& self,
                                           ModemPowerSaveParam psp);

 private:
  auto Pipeline();
  void RunPipeline();

  AeContext ae_context_;
  Sim7070AtModem* self_;
  ModemPowerSaveParam psp_;
};

class ModemPowerOffOperation final : public ModemOperation {
 public:
  explicit ModemPowerOffOperation(AeContext const& ae_context,
                                  Sim7070AtModem& self);

 private:
  auto Pipeline();
  void RunPipeline();

  AeContext ae_context_;
  Sim7070AtModem* self_;
  AtSupport& at_support_;
};
}  // namespace sim7070_modem_internal

class Sim7070AtModem final : public IModemDriver {
  friend class sim7070_modem_internal::OpenNetworkOperationImpl;
  friend class sim7070_modem_internal::CloseNetworkOperationImpl;
  friend class sim7070_modem_internal::WriteOperationImpl;
  friend class sim7070_modem_internal::ModemStartOperation;
  friend class sim7070_modem_internal::ModemStopOperation;
  friend class sim7070_modem_internal::ModemSetPowerSaveParamOperation;
  friend class sim7070_modem_internal::ModemPowerOffOperation;
  static constexpr std::uint16_t kModemMTU{1520};

 public:
  explicit Sim7070AtModem(AeContext const& ae_context,
                          IPoller::ptr const& poller, ModemInit modem_init);

  ModemOperation* Start() override;
  ModemOperation* Stop() override;
  OpenNetworkOperation* OpenNetwork(Protocol protocol, std::string const& host,
                                    std::uint16_t port) override;
  ModemOperation* CloseNetwork(ConnectionIndex connect_index) override;
  WriteOperation* WritePacket(ConnectionIndex connect_index,
                              std::span<std::uint8_t const> data) override;
  DataEvent::Subscriber data_event() override;

  ModemOperation* SetPowerSaveParam(ModemPowerSaveParam const& psp) override;
  ModemOperation* PowerOff() override;

 private:
  void Init();
  void SetupPoll();
  void PollEvent(std::int32_t handle);

  AeContext ae_context_;
  ModemInit modem_init_;
  std::unique_ptr<ISerialPort> serial_;
  AtSupport at_support_;
  ActionsQueue operation_queue_;
  std::set<ConnectionIndex> connections_;
  ConnectionIndex next_connection_index_{0};
  DataEvent data_event_;
  std::optional<AtListener> poll_listener_;

  std::unique_ptr<ModemOperation> modem_start_operation_;
  std::unique_ptr<ModemOperation> modem_stop_operation_;
  std::unique_ptr<ModemOperation> modem_set_psp_operation_;
  std::unique_ptr<ModemOperation> modem_poweroff_operation_;
  ActionPool<AeContext, sim7070_modem_internal::OpenNetworkOperationImpl, 10>
      open_network_pool_;
  ActionPool<AeContext, sim7070_modem_internal::CloseNetworkOperationImpl, 10>
      close_network_pool_;
  ActionPool<AeContext, sim7070_modem_internal::WriteOperationImpl, 10>
      write_pool_;

  bool initiated_;
  bool started_;
  int recv_in_queue_ = 0;
};

} /* namespace ae */
#endif
#endif  // AETHER_MODEMS_SIM7070_AT_MODEM_H_
