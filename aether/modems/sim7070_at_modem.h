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
 * @file sim7070_at_modem.h
 * @brief SIM7070 AT driver and its queued asynchronous operations.
 */

#ifndef AETHER_MODEMS_SIM7070_AT_MODEM_H_
#define AETHER_MODEMS_SIM7070_AT_MODEM_H_

#include "aether/config.h"

#if AE_SUPPORT_MODEMS && AE_ENABLE_SIM7070
#  include <memory>
#  include <set>

#  include "aether/actions/action_pool.h"
#  include "aether/actions/actions_queue.h"
#  include "aether/actions/repeatable_task.h"
#  include "aether/ae_context.h"
#  include "aether/executors/any_sender.h"
#  include "aether/poller/poller.h"
#  include "aether/serial_ports/at_support/at_support.h"
#  include "aether/serial_ports/iserial_port.h"

#  include "aether/modems/imodem_driver.h"

namespace ae {
class Sim7070AtModem;

namespace sim7070_modem_internal {
/**
 * @brief Queue a modem socket open request and publish its connection index.
 */
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
  std::int32_t open_result_{-1};
};

/**
 * @brief Queue closure of a modem socket and publish the outcome.
 */
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

/**
 * @brief Queue transmission of borrowed data on a modem socket.
 */
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

/**
 * @brief Immediately successful result for an already started modem.
 */
class ModemStartedAlreadyOperation final : public ModemOperation {
 public:
  explicit ModemStartedAlreadyOperation() { SetResult(Ok{kIgnore}); };
};

/**
 * @brief Configure the modem and establish network service.
 */
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
  bool network_registered_{false};
  bool pdp_active_{false};
  std::set<ConnectionIndex> stale_connections_;
  std::optional<AtListener> socket_state_listener_;
};

/**
 * @brief Run the modem-specific network shutdown sequence.
 */
class ModemStopOperation final : public ModemOperation {
 public:
  explicit ModemStopOperation(AeContext const& ae_context,
                              Sim7070AtModem& self);

 private:
  ex::AnySender<stdexec::set_value_t()> CloseSockets();
  auto Pipeline();
  void RunPipeline();

  AeContext ae_context_;
  Sim7070AtModem* self_;
  AtSupport& at_support_;
  std::set<ConnectionIndex> remaining_;
  bool failed_{false};
  bool pdp_active_{true};
};

/**
 * @brief Run the modem-specific power-saving configuration operation.
 */
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

/**
 * @brief Run the modem-specific power-down command sequence.
 */
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

/**
 * @brief SIM7070 AT driver and its queued asynchronous operations.
 *
 * Owns the serial port, AT dispatcher, operation queue, and action pools.
 * Construction schedules serial initialization. Call Start() to establish
 * network service, and drive the scheduler until Stop() finishes before
 * releasing the driver.
 * @see IModemDriver
 */
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
  /**
   * @brief Create the driver and schedule serial initialization.
   * @param ae_context Non-owning runtime context that must outlive the driver.
   * @param poller Poller used by the serial port implementation.
   * @param modem_init Configuration copied or moved into the driver.
   */
  explicit Sim7070AtModem(AeContext const& ae_context,
                          IPoller::ptr const& poller, ModemInit modem_init);

  /**
   * @copydoc IModemDriver::Start
   */
  ModemOperation* Start() override;
  /**
   * @brief Stop polling, close tracked sockets, and power off the modem.
   *
   * Shutdown is queued after pending operations. Socket cleanup continues after
   * individual close failures; the final operation reports cleanup errors.
   * The driver deactivates PDP context 0, sends AT+CPOWD=1 and waits up to
   * 30 seconds for NORMAL POWER DOWN before closing the serial port.
   * @return The same driver-owned stop operation on repeated calls. It remains
   * available until driver destruction.
   * The serial port is closed before completion, including on shutdown errors.
   * @note Stop is terminal for this instance. New start, open, write,
   * power-save, and power-off requests are rejected once stopping begins.
   */
  ModemOperation* Stop() override;
  /**
   * @copydoc IModemDriver::OpenNetwork
   */
  OpenNetworkOperation* OpenNetwork(Protocol protocol, std::string const& host,
                                    std::uint16_t port) override;
  /**
   * @copydoc IModemDriver::CloseNetwork
   */
  ModemOperation* CloseNetwork(ConnectionIndex connect_index) override;
  /**
   * @copydoc IModemDriver::WritePacket
   */
  WriteOperation* WritePacket(ConnectionIndex connect_index,
                              std::span<std::uint8_t const> data) override;
  /**
   * @copydoc IModemDriver::data_event
   */
  DataEvent::Subscriber data_event() override;

  /**
   * @copydoc IModemDriver::SetPowerSaveParam
   * @note Currently returns success without applying the requested parameters.
   */
  ModemOperation* SetPowerSaveParam(ModemPowerSaveParam const& psp) override;
  /**
   * @copydoc IModemDriver::PowerOff
   * @note Sends AT+CPOWD=1 and waits for NORMAL POWER DOWN (up to 30 seconds).
   */
  ModemOperation* PowerOff() override;

 private:
  friend struct Sim7070AtModemTestAccess;
  Sim7070AtModem(AeContext const& ae_context, ModemInit modem_init,
                 std::unique_ptr<ISerialPort> serial);

  static constexpr auto kNetworkOpActionPoolCapacity =
      AE_MODEM_NETWORK_OP_ACTION_POOL_CAPACITY;
  static constexpr auto kWriteActionPoolCapacity =
      AE_MODEM_WRITE_ACTION_POOL_CAPACITY;

  void Init();
  void SetupPoll();
  void PollEvent(std::int32_t handle);

  AeContext ae_context_;
  ModemInit modem_init_;
  std::unique_ptr<ISerialPort> serial_;
  AtSupport at_support_;
  ActionsQueue operation_queue_;
  std::set<ConnectionIndex> connections_;
  std::set<ConnectionIndex> opening_connections_;
  DataEvent data_event_;
  std::optional<AtListener> poll_listener_;
  std::optional<AtListener> buffer_full_listener_;
  std::optional<AtListener> connection_closed_listener_;

  std::unique_ptr<ModemOperation> modem_start_operation_;
  std::unique_ptr<ModemOperation> modem_stop_operation_;
  std::unique_ptr<ModemOperation> modem_set_psp_operation_;
  std::unique_ptr<ModemOperation> modem_poweroff_operation_;
  ActionPool<AeContext, sim7070_modem_internal::OpenNetworkOperationImpl,
             kNetworkOpActionPoolCapacity>
      open_network_pool_;
  ActionPool<AeContext, sim7070_modem_internal::CloseNetworkOperationImpl,
             kNetworkOpActionPoolCapacity>
      close_network_pool_;
  ActionPool<AeContext, sim7070_modem_internal::WriteOperationImpl,
             kWriteActionPoolCapacity>
      write_pool_;

  bool initiated_;
  bool started_;
  bool stopping_{false};
  std::set<ConnectionIndex> recv_in_queue_;
};

} /* namespace ae */
#endif
#endif  // AETHER_MODEMS_SIM7070_AT_MODEM_H_
