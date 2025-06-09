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

#include <algorithm>
#include <regex>

#include "aether/types/address_parser.h"

namespace ae {

std::optional<IpAddress> IpAddressParser::StringToIP(
    const std::string& ip_string) {
  bool result{false};
  IpAddress ip_addr{};

#if AE_SUPPORT_IPV4 == 1
  result = IsValidIpv4(ip_string);
#endif  // AE_SUPPORT_IPV4 == 1
  if (result) {
    result = StringToIPv4(ip_string, ip_addr.value.ipv4_value);
    if (result) {
      ip_addr.version = IpAddress::Version::kIpV4;
    }
  } else {
#if AE_SUPPORT_IPV6 == 1
    result = IsValidIpv6(ip_string);
    if (result) {
      result = StringToIPv6(ip_string, ip_addr.value.ipv6_value);
      if (result) {
        ip_addr.version = IpAddress::Version::kIpV6;
      }
    }
#endif  // AE_SUPPORT_IPV6 == 1
  }

  if (result) {
    return ip_addr;
  } else {
    return std::nullopt;
  }
}

#if AE_SUPPORT_IPV4 == 1
bool IpAddressParser::StringToIPv4(const std::string& ip_string,
                                   uint8_t ip_address[4]) {
  std::vector<std::string> octets;
  std::stringstream ss(ip_string);
  std::string octet;

  // Dividing the line by points
  while (std::getline(ss, octet, '.')) {
    octets.push_back(octet);
  }

  // We check that we have exactly 4 octets.
  if (octets.size() != 4) {
    return false;
  }

  // We convert each octet into a number and check its correctness.
  for (size_t i = 0; i < 4; ++i) {
    if (!IsNumber(octets[i])) {
      return false;
    }
    std::int32_t value = std::stoi(octets[i]);
    if (value < 0 || value > 255) {
      return false;
    }
    ip_address[i] = static_cast<uint8_t>(value);
  }

  return true;
}

bool IpAddressParser::IsNumber(const std::string& str) {
  for (char ch : str) {
    if (!isdigit(ch)) {
      return false;
    }
  }
  return true;
}

bool IpAddressParser::IsValidIpv4(const std::string& ip) {
  // Regular expression for IPv4 verification
  std::regex ipv4_pattern(
      R"((^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$))");
  return std::regex_match(ip, ipv4_pattern);
}
#endif  // AE_SUPPORT_IPV4 == 1

#if AE_SUPPORT_IPV6 == 1
bool IpAddressParser::StringToIPv6(const std::string& ip_string,
                                   uint8_t ip_address[16]) {
  std::vector<std::string> groups;
  std::stringstream ss(ip_string);
  std::string group;

  // Dividing the string by colons
  while (std::getline(ss, group, ':')) {
    groups.push_back(group);
  }

  // Processing an abbreviated record (::)
  size_t double_colon_pos = ip_string.find("::");
  bool has_double_colon = (double_colon_pos != std::string::npos);

  // Checking the correctness of the number of groups
  if (has_double_colon) {
    if (groups.size() > 7 || groups.size() < 2) {
      return false;
    }
  } else {
    if (groups.size() != 8) {
      return false;
    }
  }

  // Filling groups with zeros if there is an abbreviated entry.
  if (has_double_colon) {
    std::vector<std::string> expanded_groups(8, "0000");
    size_t left_count =
        double_colon_pos == 0
            ? 0
            : static_cast<size_t>(std::count(
                  ip_string.begin(),
                  ip_string.begin() + static_cast<std::string::difference_type>(
                                          double_colon_pos),
                  ':'));
    size_t right_count =
        groups.size() - left_count - (has_double_colon ? 1 : 0);

    for (size_t i = 0; i < left_count; ++i) {
      expanded_groups[i] = groups[i];
    }
    for (size_t i = 0; i < right_count; ++i) {
      expanded_groups[7 - right_count + i + 1] =
          groups[left_count + i + (has_double_colon ? 1 : 0)];
    }
    groups = expanded_groups;
  }

  // Converting each group to a number
  for (size_t i = 0; i < 8; ++i) {
    if (groups[i].empty()) {
      groups[i] = "0000";
    }
    if (groups[i].size() > 4) {
      return false;
    }
    for (char ch : groups[i]) {
      if (!IsHexDigit(ch)) {
        return false;
      }
    }
    // Converting a hexadecimal string to a 2 numbers
    uint16_t value = 0;
    for (char ch : groups[i]) {
      value = static_cast<uint16_t>(
          value * 16 + (isdigit(ch) ? (ch - '0') : (tolower(ch) - 'a' + 10)));
    }
    ip_address[i * 2 + 0] = static_cast<uint8_t>((value >> 8) & 0xFF);
    ip_address[i * 2 + 1] = static_cast<uint8_t>((value >> 0) & 0xFF);
  }

  return true;
}

bool IpAddressParser::IsHexDigit(const char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
         (ch >= 'A' && ch <= 'F');
}

bool IpAddressParser::IsValidIpv6(const std::string& ip) {
  // Regular expression for IPv6 verification
  std::regex ipv6_pattern(
      R"(([0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]+|::(ffff(:0{1,4})?:)?((25[0-5]|(2[0-4]|1?[0-9])?[0-9])\.){3}(25[0-5]|(2[0-4]|1?[0-9])?[0-9]|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1?[0-9])?[0-9])\.){3}(25[0-5]|(2[0-4]|1?[0-9])?[0-9])))");
  return std::regex_match(ip, ipv6_pattern);
}
#endif  // AE_SUPPORT_IPV6 == 1

}  // namespace ae
