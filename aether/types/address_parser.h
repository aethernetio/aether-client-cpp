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

#ifndef AETHER_TYPES_ADDRESS_PARSER_H_
#define AETHER_TYPES_ADDRESS_PARSER_H_

#include <string>
#include <optional>

#include "aether/config.h"
#include "aether/types/variant_type.h"
#include "aether/types/address.h"

namespace ae {
class IpAddressParser {
 public:
  IpAddressParser() = default;
  ~IpAddressParser() = default;
  std::optional<IpAddress> StringToIP(const std::string& ip_string);

 private:
#if AE_SUPPORT_IPV4 == 1
  bool StringToIPv4(const std::string& ip_string, uint8_t ip_address[4]);
  bool IsNumber(const std::string& str);
  bool IsValidIpv4(const std::string& ip);
#endif  // AE_SUPPORT_IPV4 == 1
#if AE_SUPPORT_IPV6 == 1
  bool StringToIPv6(const std::string& ip_string, uint8_t ip_address[16]);
  bool IsHexDigit(const char ch);
  bool IsValidIpv6(const std::string& ip);
#endif  // AE_SUPPORT_IPV6 == 1
};

}  // namespace ae

#endif  // AETHER_TYPES_ADDRESS_PARSER_H_
