/*
 * Copyright 2024 Aethernet Inc.
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

#ifndef AETHER_ADDRESS_PARSER_H_
#define AETHER_ADDRESS_PARSER_H_

#include <string>

#include "aether/config.h"
#include "aether/variant_type.h"
#include "aether/address.h"

namespace ae {
class IpAddressParser {
 public:
  IpAddressParser() = default;
  ~IpAddressParser() = default;
  bool stringToIP(const std::string& ipString, IpAddress& ipAddr);

 private:
#if AE_SUPPORT_IPV4 == 1
  bool stringToIPv4(const std::string& ipString, uint8_t ipAddress[4]);
  bool isNumber(const std::string& str);
#endif  // AE_SUPPORT_IPV4 == 1
#if AE_SUPPORT_IPV6 == 1
  bool stringToIPv6(const std::string& ipString, uint8_t ipAddress[16]);
  bool isHexDigit(char ch);
#endif  // AE_SUPPORT_IPV6 == 1
};

}  // namespace ae

#endif  // AETHER_ADDRESS_PARSER_H_
