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
 * @file thingy91x_at_modem.h
 * @brief Thingy91X AT driver and its asynchronous socket operations.
 */

#ifndef AETHER_MODEMS_THINGY91X_AT_MODEM_H_
#define AETHER_MODEMS_THINGY91X_AT_MODEM_H_

#include "aether/config.h"
#if AE_SUPPORT_MODEMS && AE_ENABLE_THINGY91X
#  include <memory>
#  include <set>

#  include "aether/actions/action_pool.h"
#  include "aether/actions/actions_queue.h"
#  include "aether/actions/repeatable_task.h"
#  include "aether/ae_context.h"
#  include "aether/poller/poller.h"
#  include "aether/serial_ports/at_support/at_support.h"
#  include "aether/serial_ports/iserial_port.h"

#  include "aether/modems/imodem_driver.h"

namespace ae {
class Thingy91xAtModem;

namespace thingy91x_modem_internal {
/**
 * @brief Queue a modem socket open request and publish its connection index.
 */
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

/**
 * @brief Queue closure of a modem socket and publish the outcome.
 */
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

/**
 * @brief Queue transmission of borrowed data on a modem socket.
 */
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

/**
 * @brief Thingy91X AT driver and its asynchronous socket operations.
 *
 * Owns the serial port, AT dispatcher, operation queue, and action pools.
 * Construction schedules serial initialization. Call Start() to establish
 * network service, and drive the scheduler until Stop() finishes before
 * releasing the driver.
 * @see IModemDriver
 */
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
  /**
   * @brief Create the driver and schedule serial initialization.
   * @param ae_context Non-owning runtime context that must outlive the driver.
   * @param poller Poller used by the serial port implementation.
   * @param modem_init Configuration copied or moved into the driver.
   */
  explicit Thingy91xAtModem(AeContext const& ae_context,
                            IPoller::ptr const& poller, ModemInit modem_init);

  /**
   * @copydoc IModemDriver::Start
   */
  ModemOperation* Start() override;
  /**
   * @brief Stop polling, close tracked sockets, and disable network service.
   *
   * Shutdown is queued after pending operations. Socket cleanup continues after
   * individual close failures; the final operation reports cleanup errors.
   * The driver selects and closes each socket before sending AT+CFUN=0.
   * @return The same driver-owned stop operation on repeated calls. It remains
   * available until driver destruction.
   * @note Stop is terminal for this instance. New start, open, write,
   * power-save, and power-off requests are rejected once stopping begins.
   */
  ModemOperation* Stop() override;
  /**
   * @copydoc IModemDriver::OpenNetwork
   */
  OpenNetworkOperation* OpenNetwork(ae::Protocol protocol,
                                    std::string const& host,
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
   * @note Applies supported radio and power-saving settings through AT
   * commands.
   */
  ModemOperation* SetPowerSaveParam(ModemPowerSaveParam const& psp) override;
  /**
   * @copydoc IModemDriver::PowerOff
   * @note Sends AT+CFUN=0 to enter minimum functionality; board power remains
   * on.
   */
  ModemOperation* PowerOff() override;

 private:
  friend struct Thingy91xAtModemTestAccess;
  Thingy91xAtModem(AeContext const& ae_context, ModemInit modem_init,
                   std::unique_ptr<ISerialPort> serial);

  static constexpr auto kNetworkOpActionPoolCapacity =
      AE_MODEM_NETWORK_OP_ACTION_POOL_CAPACITY;
  static constexpr auto kWriteActionPoolCapacity =
      AE_MODEM_WRITE_ACTION_POOL_CAPACITY;

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
  ActionPool<AeContext, thingy91x_modem_internal::OpenNetworkOperationImpl,
             kNetworkOpActionPoolCapacity>
      open_network_pool_;
  ActionPool<AeContext, thingy91x_modem_internal::CloseNetworkOperationImpl,
             kNetworkOpActionPoolCapacity>
      close_network_pool_;
  ActionPool<AeContext, thingy91x_modem_internal::WriteOperationImpl,
             kWriteActionPoolCapacity>
      write_pool_;

  std::set<ConnectionIndex> connections_;
  DataEvent data_event_;
  std::optional<RepeatableTask<AeContext>> poll_task_;
  std::optional<AtListener> poll_listener_;
  bool initiated_;
  bool started_;
  bool stopping_{false};
  int poll_in_queue_ = 0;
  int recv_in_queue_ = 0;
};

} /* namespace ae */
#endif
#endif  // AETHER_MODEMS_THINGY91X_AT_MODEM_H_
