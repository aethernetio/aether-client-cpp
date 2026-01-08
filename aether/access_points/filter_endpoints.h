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

#ifndef AETHER_ACCESS_POINTS_FILTER_ENDPOINTS_H_
#define AETHER_ACCESS_POINTS_FILTER_ENDPOINTS_H_

#include <array>
#include <algorithm>

#include "aether/config.h"
#include "aether/types/address.h"

namespace ae {
template <AddrVersion... supported_versions>
bool FilterAddresses(Endpoint const& address) {
  auto addr_version = address.address.Index();
#if !AE_SUPPORT_CLOUD_DNS
  if (addr_version == AddrVersion::kNamed) {
    return false;
  }
#endif
#if !AE_SUPPORT_IPV4
  if (addr_version == AddrVersion::kIpV4) {
    return false;
  }
#endif
#if !AE_SUPPORT_IPV6
  if (addr_version == AddrVersion::kIpV6) {
    return false;
  }
#endif
  static constexpr std::array supported = {supported_versions...};
  return std::any_of(std::begin(supported), std::end(supported),
                     [addr_version](auto v) { return v == addr_version; });
}

/**
 * \brief Check if protocol in address is supported.
 * \return True if protocol is supported, false otherwise.
 */
template <Protocol... supported_protocols>
bool FilterProtocol(Endpoint const& address) {
  auto protocol = address.protocol;

#if !AE_SUPPORT_TCP
  if (protocol == Protocol::kTcp) {
    return false;
  }
#endif
#if !AE_SUPPORT_UDP
  if (protocol == Protocol::kUdp) {
    return false;
  }
#endif
  static constexpr std::array supported = {supported_protocols...};
  return std::any_of(std::begin(supported), std::end(supported),
                     [protocol](auto p) { return p == protocol; });
}
}  // namespace ae

#endif  // AETHER_ACCESS_POINTS_FILTER_ENDPOINTS_H_
