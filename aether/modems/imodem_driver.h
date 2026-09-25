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
 * @file imodem_driver.h
 * @brief Asynchronous modem driver interface and operation result types.
 */

#ifndef AETHER_MODEMS_IMODEM_DRIVER_H_
#define AETHER_MODEMS_IMODEM_DRIVER_H_

#include "aether/config.h"

#if AE_SUPPORT_MODEMS
#  include <cstdint>
#  include <optional>
#  include <span>
#  include <string>

#  include "aether-miscpp/meta/ignore_t.h"
#  include "aether-miscpp/types/result.h"
#  include "aether/actions/action.h"
#  include "aether/events/events.h"
#  include "aether/modems/modem_driver_types.h"
#  include "aether/types/address.h"
#  include "aether/types/data_buffer.h"

namespace ae {
/**
 * @brief Driver-specific operation error code.
 *
 * Interpret the numeric value in the context of the selected modem driver.
 */
enum class ModemError : int {};

/**
 * @brief Asynchronous socket opening result: a connection index or an error.
 */
class OpenNetworkOperation : public Action {
 public:
  // return either connection index or error
  using ResultType = Result<ConnectionIndex, ModemError>;
  using ResultEvent = Event<void(ResultType)>;
  /**
   * @brief Subscribe to the terminal result, emitted before Action::Finish().
   * @note Retain a Subscription to control the callback lifetime.
   */
  ResultEvent::Subscriber result_event() {
    return EventSubscriber{result_event_};
  }
  /**
   * @brief Return the stored result, or an empty optional before completion.
   */
  std::optional<ResultType> const& result() const { return result_; }

 protected:
  /**
   * @brief Store and publish the result, then finish the action.
   * @warning Completion may destroy this action; do not access it afterwards.
   */
  void SetResult(ResultType&& res) {
    result_.emplace(std::move(res));
    result_event_.Emit(result_.value());
    Finish();
  }

 private:
  std::optional<ResultType> result_;
  ResultEvent result_event_;
};

/**
 * @brief Asynchronous write result: the number of bytes written or an error.
 */
class WriteOperation : public Action {
 public:
  // return size of bytes written, or error code
  using ResultType = Result<std::size_t, ModemError>;
  using ResultEvent = Event<void(ResultType)>;
  /**
   * @brief Subscribe to the terminal result, emitted before Action::Finish().
   * @note Retain a Subscription to control the callback lifetime.
   */
  ResultEvent::Subscriber result_event() {
    return EventSubscriber{result_event_};
  }
  /**
   * @brief Return the stored result, or an empty optional before completion.
   */
  std::optional<ResultType> const& result() const { return result_; }

 protected:
  /**
   * @brief Store and publish the result, then finish the action.
   * @warning Completion may destroy this action; do not access it afterwards.
   */
  void SetResult(ResultType&& res) {
    result_.emplace(std::move(res));
    result_event_.Emit(result_.value());
    Finish();
  }

 private:
  std::optional<ResultType> result_;
  ResultEvent result_event_;
};

/**
 * @brief Asynchronous control operation with success or error completion.
 */
class ModemOperation : public Action {
 public:
  using ResultType = Result<Ignore, ModemError>;
  using ResultEvent = Event<void(ResultType)>;
  /**
   * @brief Subscribe to the terminal result, emitted before Action::Finish().
   * @note Retain a Subscription to control the callback lifetime.
   */
  ResultEvent::Subscriber result_event() {
    return EventSubscriber{result_event_};
  }
  /**
   * @brief Return the stored result, or an empty optional before completion.
   */
  std::optional<ResultType> const& result() const { return result_; }

 protected:
  /**
   * @brief Store and publish the result, then finish the action.
   * @warning Completion may destroy this action; do not access it afterwards.
   */
  void SetResult(ResultType&& res) {
    result_.emplace(std::move(res));
    result_event_.Emit(result_.value());
    Finish();
  }

 private:
  std::optional<ResultType> result_;
  ResultEvent result_event_;
};

/**
 * @brief Runtime interface for asynchronous cellular modem control and socket
 * I/O.
 *
 * Returned action pointers are non-owning. The driver owns the operations;
 * callers must keep the driver and its scheduler context alive while they run.
 * An operation can already be complete when returned: inspect result() before
 * subscribing to result_event(). Operation pointers must not be retained after
 * the owner reclaims or replaces a completed operation.
 *
 * Keep driving the scheduler until shutdown completes before destroying the
 * driver. Supported settings and shutdown behavior depend on the
 * implementation.
 */
class IModemDriver {
 public:
  using DataEvent = Event<void(ConnectionIndex, DataBuffer const& data)>;
  using ConnectionClosedEvent = Event<void(ConnectionIndex)>;

  // Drivers emit this when they observe an unsolicited socket closure.
  // Retain a Subscription and defer work that could destroy the driver.
  ConnectionClosedEvent::Subscriber connection_closed_event() {
    return EventSubscriber{connection_closed_event_};
  }

  virtual ~IModemDriver() = default;

  /**
   * @brief Initialize network service and prepare the modem for connections.
   * @return A driver-owned operation, or nullptr if the request is rejected.
   */
  virtual ModemOperation* Start() = 0;
  /**
   * @brief Request asynchronous shutdown of network service.
   * @return A driver-owned shutdown operation, or nullptr if unavailable.
   * @note Await completion before releasing the driver. See the concrete driver
   * for socket cleanup and restart semantics.
   */
  virtual ModemOperation* Stop() = 0;

  /**
   * @brief Open a TCP or UDP connection to a remote endpoint.
   * @param protocol Transport protocol to use.
   * @param host Remote host name or address supported by the driver.
   * @param port Remote port number.
   * @return A driver-owned operation yielding a connection index, or nullptr
   * if no operation can be created.
   */
  virtual OpenNetworkOperation* OpenNetwork(Protocol protocol,
                                            std::string const& host,
                                            std::uint16_t port) = 0;

  /**
   * @brief Request closure of a modem connection.
   * @param connect_index Connection index returned by OpenNetwork().
   * @return A driver-owned operation, or nullptr if no operation is available.
   */
  virtual ModemOperation* CloseNetwork(ConnectionIndex connect_index) = 0;

  /**
   * @brief Queue data for transmission on an open connection.
   * @param connect_index Destination connection index.
   * @param data Borrowed bytes; keep the buffer valid until the operation
   * finishes.
   * @return A driver-owned write operation, or nullptr if the request is
   * rejected.
   */
  virtual WriteOperation* WritePacket(ConnectionIndex connect_index,
                                      std::span<std::uint8_t const> data) = 0;
  /**
   * @brief Subscribe to received data and its originating connection index.
   * @note The buffer reference is valid only during the callback; copy data
   * that must be retained. Keep a Subscription for the desired callback
   * lifetime.
   */
  virtual DataEvent::Subscriber data_event() = 0;

  /**
   * @brief Request implementation-specific power-saving settings.
   * @param psp Requested radio and power-saving parameters.
   * @return A driver-owned operation, or nullptr if the request is rejected.
   * @note Some drivers implement this as a successful no-op.
   */
  virtual ModemOperation* SetPowerSaveParam(ModemPowerSaveParam const& psp) = 0;
  /**
   * @brief Request the driver's power-down sequence.
   * @return A driver-owned operation, or nullptr if the request is rejected.
   * @note This is distinct from Stop(); it does not promise socket cleanup or
   * physical removal of power. See the concrete implementation.
   */
  virtual ModemOperation* PowerOff() = 0;

 protected:
  void NotifyConnectionClosed(ConnectionIndex connection) {
    connection_closed_event_.Emit(connection);
  }

 private:
  ConnectionClosedEvent connection_closed_event_;
};

} /* namespace ae */
#endif
#endif  // AETHER_MODEMS_IMODEM_DRIVER_H_
