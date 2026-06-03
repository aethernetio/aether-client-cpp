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

#ifndef AETHER_MODEMS_THINGY91X_AT_MODEM_H_
#define AETHER_MODEMS_THINGY91X_AT_MODEM_H_

#include "aether/config.h"
#if AE_SUPPORT_MODEMS && AE_ENABLE_THINGY91X
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
class Thingy91xAtModem;

namespace thingy91x_modem_internal {
class OpenNetworkOperationImpl final : public OpenNetworkOperation {
 public:
  OpenNetworkOperationImpl(AeContext const& ae_context, Thingy91xAtModem& self,
                           ae::Protocol protocol, std::string host,
                           std::uint16_t port);

 private:
  auto Pipeline();
  void RunPipeline();

  AeContext ae_context_;
  Thingy91xAtModem* self_;
  AtSupport& at_support_;
  Protocol protocol_;
  std::string host_;
  std::uint16_t port_;
  std::int32_t handle_{-1};
};

class CloseNetworkOperationImpl final : public ModemOperation {
 public:
  CloseNetworkOperationImpl(AeContext const& ae_context, Thingy91xAtModem& self,
                            ConnectionIndex connect_index);

 private:
  auto Pipeline();
  void RunPipeline();

  AeContext ae_context_;
  Thingy91xAtModem* self_;
  AtSupport& at_support_;
  ConnectionIndex connect_index_;
};

class WriteOperationImpl final : public WriteOperation {
 public:
  WriteOperationImpl(AeContext const& ae_context, Thingy91xAtModem& self,
                     ConnectionIndex connect_index,
                     std::span<std::uint8_t const> data);

 private:
  auto Pipeline();
  void RunPipeline();

  AeContext ae_context_;
  Thingy91xAtModem* self_;
  AtSupport& at_support_;
  ConnectionIndex connect_index_;
  std::span<std::uint8_t const> data_;
};

class ModemStartOperation;
class ModemStopOperation;
class ModemSetPowerSaveParamOperation;
class ModemPowerOffOperation;
}  // namespace thingy91x_modem_internal

class Thingy91xAtModem final : public IModemDriver {
  friend class thingy91x_modem_internal::OpenNetworkOperationImpl;
  friend class thingy91x_modem_internal::CloseNetworkOperationImpl;
  friend class thingy91x_modem_internal::WriteOperationImpl;
  friend class thingy91x_modem_internal::ModemStartOperation;
  friend class thingy91x_modem_internal::ModemStopOperation;
  friend class thingy91x_modem_internal::ModemSetPowerSaveParamOperation;
  friend class thingy91x_modem_internal::ModemPowerOffOperation;

  static constexpr std::uint16_t kModemMTU{1024};

 public:
  explicit Thingy91xAtModem(AeContext const& ae_context,
                            IPoller::ptr const& poller, ModemInit modem_init);

  ModemOperation* Start() override;
  ModemOperation* Stop() override;
  OpenNetworkOperation* OpenNetwork(ae::Protocol protocol,
                                    std::string const& host,
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
  void PollEvent(std::int32_t handle, std::string_view flags);

  AeContext ae_context_;
  ModemInit modem_init_;
  std::unique_ptr<ISerialPort> serial_;
  AtSupport at_support_;
  ActionsQueue operation_queue_;

  std::unique_ptr<ModemOperation> modem_start_operation_;
  std::unique_ptr<ModemOperation> modem_stop_operation_;
  std::unique_ptr<ModemOperation> modem_set_psp_operation_;
  std::unique_ptr<ModemOperation> modem_poweroff_operation_;
  ActionPool<AeContext, thingy91x_modem_internal::OpenNetworkOperationImpl, 10>
      open_network_pool_;
  ActionPool<AeContext, thingy91x_modem_internal::CloseNetworkOperationImpl, 10>
      close_network_pool_;
  ActionPool<AeContext, thingy91x_modem_internal::WriteOperationImpl, 10>
      write_pool_;

  std::set<ConnectionIndex> connections_;
  DataEvent data_event_;
  std::optional<RepeatableTask<AeContext>> poll_task_;
  std::optional<AtListener> poll_listener_;
  bool initiated_;
  bool started_;
  int poll_in_queue_ = 0;
  int recv_in_queue_ = 0;
};

} /* namespace ae */
#endif
#endif  // AETHER_MODEMS_THINGY91X_AT_MODEM_H_
