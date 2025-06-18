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

#ifndef AETHER_ADAPTERS_MODEM_ADAPTER_H_
#define AETHER_ADAPTERS_MODEM_ADAPTER_H_

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
  AE_REFLECT_MEMBERS(port_name, baud_rate, pin, use_pin, operator_name,
                     apn_name, apn_user, apn_pass, modem_mode, auth_type,
                     use_auth, auth_user, auth_pass, ssl_cert, use_ssl)
  std::string port_name;
  std::uint32_t baud_rate;
  std::uint8_t pin[4];
  bool use_pin;
  std::string operator_name;
  std::string apn_name;
  std::string apn_user;
  std::string apn_pass;
  kModemMode modem_mode;
  kAuthType auth_type;
  bool use_auth;
  std::string auth_user;
  std::string auth_pass;
  std::string ssl_cert;
  bool use_ssl;
};

}  // namespace ae

#endif  // AETHER_ADAPTERS_MODEM_ADAPTER_H_