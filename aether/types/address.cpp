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

#include "aether/types/address.h"

#include <algorithm>
#include <iterator>

namespace ae {
bool operator==(const IpAddress& left, const IpAddress& right) {
  using Version = IpAddress::Version;

  if (left.version != right.version) {
    return false;
  }
  switch (left.version) {
    case Version::kIpV6:
#if AE_SUPPORT_IPV6
      return std::equal(std::begin(left.value.ipv6_value),
                        std::end(left.value.ipv6_value),
                        std::begin(right.value.ipv6_value));
#else
      assert(false);
      return false;
#endif
    case Version::kIpV4:
#if AE_SUPPORT_IPV4
      return std::equal(std::begin(left.value.ipv4_value),
                        std::end(left.value.ipv4_value),
                        std::begin(right.value.ipv4_value));
#else
      assert(false);
      return false;
#endif

    default:
      return false;
  }
}
bool operator!=(const IpAddress& left, const IpAddress& right) {
  return !(left == right);
}

bool operator<(const IpAddress& left, const IpAddress& right) {
  using Version = IpAddress::Version;
  if (left.version == right.version) {
    switch (left.version) {
      case Version::kIpV6: {
#if AE_SUPPORT_IPV6
        return std::lexicographical_compare(std::begin(left.value.ipv6_value),
                                            std::end(left.value.ipv6_value),
                                            std::begin(right.value.ipv6_value),
                                            std::end(right.value.ipv6_value));
#else
        assert(false);
        return false;
#endif
      }
      case Version::kIpV4: {
#if AE_SUPPORT_IPV4
        return std::lexicographical_compare(std::begin(left.value.ipv4_value),
                                            std::end(left.value.ipv4_value),
                                            std::begin(right.value.ipv4_value),
                                            std::end(right.value.ipv4_value));
#else
        assert(false);
        return false;
#endif
      }
    }
  }

  return left.version < right.version;
}

void IpAddress::set_value(const std::uint8_t* val) {
  switch (version) {
    case Version::kIpV6:
#if AE_SUPPORT_IPV6
      std::copy(val, val + 16, std::begin(value.ipv6_value));
#else
      assert(false);
#endif
      break;
    case Version::kIpV4:
#if AE_SUPPORT_IPV4
      std::copy(val, val + 4, std::begin(value.ipv4_value));
#else
      assert(false);
#endif
      break;
    default:
      assert(false);
  }
}

bool operator<(IpAddressPortProtocol const& left,
               IpAddressPortProtocol const& right) {
  if (left.ip == right.ip) {
    if (left.port == right.port) {
      return left.protocol < right.protocol;
    }
    return left.port < right.port;
  }
  return left.ip < right.ip;
}

}  // namespace ae
