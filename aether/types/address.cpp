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

#include <iterator>
#include <algorithm>

namespace ae {
bool operator==([[maybe_unused]] IpV4Addr const& left,
                [[maybe_unused]] IpV4Addr const& right) {
#if AE_SUPPORT_IPV4 == 1
  {
    return std::equal(std::begin(left.ipv4_value), std::end(left.ipv4_value),
                      std::begin(right.ipv4_value));
  }
#else
  return false;
#endif
}
bool operator!=(IpV4Addr const& left, IpV4Addr const& right) {
  return !(left == right);
}

bool operator<([[maybe_unused]] IpV4Addr const& left,
               [[maybe_unused]] IpV4Addr const& right) {
#if AE_SUPPORT_IPV4 == 1
  return std::lexicographical_compare(
      std::begin(left.ipv4_value), std::end(left.ipv4_value),
      std::begin(right.ipv4_value), std::end(right.ipv4_value));
#else
  return false;
#endif
}

bool operator==([[maybe_unused]] IpV6Addr const& left,
                [[maybe_unused]] IpV6Addr const& right) {
#if AE_SUPPORT_IPV6 == 1
  return std::equal(std::begin(left.ipv6_value), std::end(left.ipv6_value),
                    std::begin(right.ipv6_value));
#else
  return false;
#endif
}

bool operator!=(IpV6Addr const& left, IpV6Addr const& right) {
  return !(left == right);
}

bool operator<([[maybe_unused]] IpV6Addr const& left,
               [[maybe_unused]] IpV6Addr const& right) {
#if AE_SUPPORT_IPV6 == 1
  return std::lexicographical_compare(
      std::begin(left.ipv6_value), std::end(left.ipv6_value),
      std::begin(right.ipv6_value), std::end(right.ipv6_value));
#else
  return false;
#endif
}

bool operator==(NamedAddr const& left, NamedAddr const& right) {
  return left.name == right.name;
}
bool operator!=(NamedAddr const& left, NamedAddr const& right) {
  return left.name != right.name;
}
bool operator<(NamedAddr const& left, NamedAddr const& right) {
  return left.name < right.name;
}

bool operator==(const Address& left, const Address& right) {
  if (left.Index() != right.Index()) {
    return false;
  }

  switch (left.Index()) {
    case AddrVersion::kIpV4:
#if AE_SUPPORT_IPV4 == 1
    {
      return left.Get<IpV4Addr>() == right.Get<IpV4Addr>();
    }
#endif
    break;
    case AddrVersion::kIpV6:
#if AE_SUPPORT_IPV6 == 1
    {
      return left.Get<IpV6Addr>() == right.Get<IpV6Addr>();
    }
#endif
    break;
    case AddrVersion::kNamed:
#if AE_SUPPORT_CLOUD_DNS == 1
    {
      return left.Get<NamedAddr>() == right.Get<NamedAddr>();
    }
#endif
    break;
    default:
      break;
  }
  return false;
}

bool operator!=(const Address& left, const Address& right) {
  return !(left == right);
}

bool operator<(const Address& left, const Address& right) {
  if (left.Index() < right.Index()) {
    return true;
  }
  if (left.Index() > right.Index()) {
    return false;
  }

  switch (left.Index()) {
    case AddrVersion::kIpV4:
#if AE_SUPPORT_IPV4 == 1
    {
      return left.Get<IpV4Addr>() < right.Get<IpV4Addr>();
    }
#endif
    break;
    case AddrVersion::kIpV6:
#if AE_SUPPORT_IPV6 == 1
    {
      return left.Get<IpV6Addr>() < right.Get<IpV6Addr>();
    }
#endif
    break;
    case AddrVersion::kNamed:
#if AE_SUPPORT_CLOUD_DNS == 1
    {
      return left.Get<NamedAddr>() < right.Get<NamedAddr>();
    }
#endif
    break;
    default:
      break;
  }
  return false;
}

bool operator<(Endpoint const& left, Endpoint const& right) {
  if (left.address == right.address) {
    if (left.port == right.port) {
      return left.protocol < right.protocol;
    }
    return left.port < right.port;
  }
  return left.address < right.address;
}

}  // namespace ae
