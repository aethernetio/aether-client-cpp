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

#include "aether/address_parser.h"

namespace ae {
bool IpAddressParser::stringToIP(const std::string& ipString,
                                 IpAddress& ipAddr) {
  bool result{false};

  if (ipAddr.version == IpAddress::Version::kIpV4) {
#if AE_SUPPORT_IPV4 == 1
    result = stringToIPv4(ipString, ipAddr.value.ipv4_value);
#endif  // AE_SUPPORT_IPV4 == 1
  } else if (ipAddr.version == IpAddress::Version::kIpV6) {
#if AE_SUPPORT_IPV6 == 1
    result = stringToIPv6(ipString, ipAddr.value.ipv6_value);
#endif  // AE_SUPPORT_IPV6 == 1
  }

  return result;
}

#if AE_SUPPORT_IPV4 == 1
bool IpAddressParser::stringToIPv4(const std::string& ipString,
                                   uint8_t ipAddress[4]) {
  std::vector<std::string> octets;
  std::stringstream ss(ipString);
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
  for (int i = 0; i < 4; ++i) {
    if (!isNumber(octets[i])) {
      return false;
    }
    int value = std::stoi(octets[i]);
    if (value < 0 || value > 255) {
      return false;
    }
    ipAddress[i] = static_cast<uint8_t>(value);
  }

  return true;
}

bool IpAddressParser::isNumber(const std::string& str) {
  for (char ch : str) {
    if (!isdigit(ch)) {
      return false;
    }
  }
  return true;
}
#endif  // AE_SUPPORT_IPV4 == 1

#if AE_SUPPORT_IPV6 == 1
bool IpAddressParser::stringToIPv6(const std::string& ipString,
                                   uint8_t ipAddress[16]) {
  std::vector<std::string> groups;
  std::stringstream ss(ipString);
  std::string group;

  // Dividing the string by colons
  while (std::getline(ss, group, ':')) {
    groups.push_back(group);
  }

  // Processing an abbreviated record (::)
  size_t doubleColonPos = ipString.find("::");
  bool hasDoubleColon = (doubleColonPos != std::string::npos);

  // Checking the correctness of the number of groups
  if (hasDoubleColon) {
    if (groups.size() > 7 || groups.size() < 2) {
      return false;
    }
  } else {
    if (groups.size() != 8) {
      return false;
    }
  }

  // Filling groups with zeros if there is an abbreviated entry.
  if (hasDoubleColon) {
    std::vector<std::string> expandedGroups(8, "0000");
    size_t leftCount = doubleColonPos == 0
                           ? 0
                           : std::count(ipString.begin(),
                                        ipString.begin() + doubleColonPos, ':');
    size_t rightCount = groups.size() - leftCount - (hasDoubleColon ? 1 : 0);

    for (size_t i = 0; i < leftCount; ++i) {
      expandedGroups[i] = groups[i];
    }
    for (size_t i = 0; i < rightCount; ++i) {
      expandedGroups[7 - rightCount + i + 1] =
          groups[leftCount + i + (hasDoubleColon ? 1 : 0)];
    }
    groups = expandedGroups;
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
      if (!isHexDigit(ch)) {
        return false;
      }
    }
    // Converting a hexadecimal string to a 2 numbers
    uint16_t value = 0;
    for (char ch : groups[i]) {
      value = static_cast<uint16_t>(
          value * 16 + (isdigit(ch) ? (ch - '0') : (tolower(ch) - 'a' + 10)));
    }
    ipAddress[i * 2 + 0] = static_cast<uint8_t>((value >> 8) & 0xFF);
    ipAddress[i * 2 + 1] = static_cast<uint8_t>((value >> 0) & 0xFF);
  }

  return true;
}

bool IpAddressParser::isHexDigit(char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
         (ch >= 'A' && ch <= 'F');
}
#endif  // AE_SUPPORT_IPV6 == 1

}  // namespace ae
