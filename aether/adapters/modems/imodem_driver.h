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

#ifndef AETHER_ADAPTERS_MODEMS_IMODEM_DRIVER_H_
#define AETHER_ADAPTERS_MODEMS_IMODEM_DRIVER_H_

#include <functional>
#include <string>

#include "aether/adapters/modems/serial_ports/iserial_port.h"

namespace ae {

enum class kModemMode : std::uint8_t {
  kModeAuto = 2,
  kModeGSMOnly = 13,
  kModeLTEOnly = 38,
  kModeGSMLTE = 51
};

enum class kAuthType : std::uint8_t {
  kAuthTypeNone = 0,
  kAuthTypePAP = 1,
  kAuthTypeCHAP = 2,
  kAuthTypePAPCHAP = 3
};

struct ModemInit {
  AE_REFLECT_MEMBERS(serial_init, pin, use_pin, operator_name, apn_name,
                     apn_user, apn_pass, modem_mode, auth_type, use_auth,
                     auth_user, auth_pass, ssl_cert, use_ssl)
  SerialInit serial_init;
  std::uint8_t pin[4];
  bool use_pin;
  kModemMode modem_mode;
  std::string operator_name;
  std::string apn_name;
  std::string apn_user;
  std::string apn_pass;
  kAuthType auth_type;
  bool use_auth;
  std::string auth_user;
  std::string auth_pass;
  std::string ssl_cert;
  bool use_ssl;
};

// A class for managing event subscriptions
template <typename... Args>
class EventSubscriberModem {
 public:
  void subscribe(std::function<void(Args...)> callback) {
    callbacks_.push_back(std::move(callback));
  }

  std::vector<std::function<void(Args...)>> GetCallbacks() {
    return callbacks_;
  }

 private:
  friend class EventEmitter;
  std::vector<std::function<void(Args...)>> callbacks_;
};

// A class for generating events
template <typename... Args>
class EventEmitterModem {
 public:
  EventSubscriberModem<Args...>& subscriber() { return subscriber_; }

  void notify(Args... args) {
    auto callbacks = subscriber_.GetCallbacks();
    for (auto& callback : callbacks) {
      callback(args...);
    }
  }

 private:
  EventSubscriberModem<Args...> subscriber_;
};

class IModemDriver {

 public:
  IModemDriver() = default;
  virtual ~IModemDriver() = default;

  virtual void Init() = 0;
  virtual void Setup() = 0;
  virtual void Stop() = 0;

  virtual EventSubscriberModem<>& initiated() = 0;
  virtual EventSubscriberModem<const std::string&>& errorOccurred() = 0;
};

} /* namespace ae */

#endif  // AETHER_ADAPTERS_MODEMS_IMODEM_DRIVER_H_