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

#ifndef AETHER_ADAPTERS_PARENT_MODEM_H_
#define AETHER_ADAPTERS_PARENT_MODEM_H_

#include <string>

#include "aether/aether.h"
#include "aether/adapters/adapter.h"
#include "aether/poller/poller.h"

namespace ae {
class Aether;

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

struct SerialInit {
  AE_REFLECT_MEMBERS(port_name, baud_rate)
  std::string port_name;
  std::uint32_t baud_rate;
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

class ParentModemAdapter : public Adapter {
  AE_OBJECT(ParentModemAdapter, Adapter, 0)

 protected:
  ParentModemAdapter() = default;

 public:
#ifdef AE_DISTILLATION
  ParentModemAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
                     ModemInit modem_init, Domain* domain);
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, poller_, modem_init_))

  Obj::ptr aether_;
  IPoller::ptr poller_;  

  ModemInit modem_init_;
};
}  // namespace ae

#endif  // AETHER_ADAPTERS_PARENT_MODEM_H_
