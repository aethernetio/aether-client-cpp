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

#include "aether/adapters/adapter.h"
#include "aether/poller/poller.h"

namespace ae {
class Aether;

struct ModemInit {
  std::uint8_t pin[4];
  bool use_pin;
  std::string operator_name;
  std::string apn_name;
  std::string apn_user;
  std::string apn_password;
  kAuthType auth_type;
  bool use_auth;
  std::string ssl_cert;
  bool use_ssl;
};

class ParentModemAdapter : public Adapter {
  AE_OBJECT(ParentModemAdapter, Adapter, 0)

 protected:
  ParentModemAdapter() = default;

 public:
#ifdef AE_DISTILLATION
  ParentWifiAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
                    std::string ssid, std::string pass, Domain* domain);
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, poller_, modem_init_))

  Obj::ptr aether_;
  IPoller::ptr poller_;

  ModemInit modem_init_;
};
}  // namespace ae

#endif  // AETHER_ADAPTERS_PARENT_MODEM_H_
