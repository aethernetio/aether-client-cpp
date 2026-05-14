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

#ifndef AETHER_MODEMS_IMODEM_DRIVER_H_
#define AETHER_MODEMS_IMODEM_DRIVER_H_

#include "aether/config.h"

#if AE_SUPPORT_MODEMS
#  include <span>
#  include <string>
#  include <cstdint>
#  include <optional>

#  include "aether/types/result.h"
#  include "aether/events/events.h"
#  include "aether/types/address.h"
#  include "aether/meta/ignore_t.h"
#  include "aether/actions/action2_.h"
#  include "aether/types/data_buffer.h"
#  include "aether/modems/modem_driver_types.h"

namespace ae {
enum class ModemError : int {};

class OpenNetworkOperation : public a2::Action {
 public:
  // return either connection index or error
  using ResultType = Result<ConnectionIndex, ModemError>;
  using ResultEvent = Event<void(ResultType)>;
  ResultEvent::Subscriber result_event() {
    return EventSubscriber{result_event_};
  }
  std::optional<ResultType> const& result() const { return result_; }

 protected:
  void SetResult(ResultType&& res) {
    result_.emplace(std::move(res));
    result_event_.Emit(result_.value());
    Finish();
  }

 private:
  std::optional<ResultType> result_;
  ResultEvent result_event_;
};

class WriteOperation : public a2::Action {
 public:
  // return size of bytes written, or error code
  using ResultType = Result<std::size_t, ModemError>;
  using ResultEvent = Event<void(ResultType)>;
  ResultEvent::Subscriber result_event() {
    return EventSubscriber{result_event_};
  }
  std::optional<ResultType> const& result() const { return result_; }

 protected:
  void SetResult(ResultType&& res) {
    result_.emplace(std::move(res));
    result_event_.Emit(result_.value());
    Finish();
  }

 private:
  std::optional<ResultType> result_;
  ResultEvent result_event_;
};

class ModemOperation : public a2::Action {
 public:
  using ResultType = Result<Ignore, ModemError>;
  using ResultEvent = Event<void(ResultType)>;
  ResultEvent::Subscriber result_event() {
    return EventSubscriber{result_event_};
  }
  std::optional<ResultType> const& result() const { return result_; }

 protected:
  void SetResult(ResultType&& res) {
    result_.emplace(std::move(res));
    result_event_.Emit(result_.value());
    Finish();
  }

 private:
  std::optional<ResultType> result_;
  ResultEvent result_event_;
};

class IModemDriver {
 public:
  using DataEvent = Event<void(ConnectionIndex, DataBuffer const& data)>;

  virtual ~IModemDriver() = default;

  virtual ModemOperation* Start() = 0;
  virtual ModemOperation* Stop() = 0;

  virtual OpenNetworkOperation* OpenNetwork(Protocol protocol,
                                            std::string const& host,
                                            std::uint16_t port) = 0;

  virtual ModemOperation* CloseNetwork(ConnectionIndex connect_index) = 0;

  virtual WriteOperation* WritePacket(ConnectionIndex connect_index,
                                      std::span<std::uint8_t const> data) = 0;
  virtual DataEvent::Subscriber data_event() = 0;

  virtual ModemOperation* SetPowerSaveParam(ModemPowerSaveParam const& psp) = 0;
  virtual ModemOperation* PowerOff() = 0;
};

} /* namespace ae */
#endif
#endif  // AETHER_MODEMS_IMODEM_DRIVER_H_
