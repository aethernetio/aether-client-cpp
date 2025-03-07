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

#ifndef AETHER_ADDRESS_H_
#define AETHER_ADDRESS_H_

#include <cstdint>
#include <string>

#include "aether/config.h"
#include "aether/type_traits.h"
#include "aether/variant_type.h"
#include "aether/reflect/reflect.h"

#include "aether/format/format.h"

namespace ae {

struct IpAddress {
  enum class Version : std::uint8_t {
    kIpV4,
    kIpV6,
  };

  Version version = Version::kIpV4;
  union {
#if AE_SUPPORT_IPV6 == 1
    std::uint8_t ipv6_value[16];
#endif  // AE_SUPPORT_IPV6 == 1
#if AE_SUPPORT_IPV4 == 1
    std::uint8_t ipv4_value[4];
#endif  // AE_SUPPORT_IPV4 == 1
  } value;

  friend bool operator==(const IpAddress& left, const IpAddress& right);
  friend bool operator!=(const IpAddress& left, const IpAddress& right);
  friend bool operator<(const IpAddress& lef, const IpAddress& right);

  void set_value(const std::uint8_t* val);
};

template <typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& s, IpAddress& ip_address) {
  s >> ip_address.version;
  switch (ip_address.version) {
    case IpAddress::Version::kIpV4:
#if AE_SUPPORT_IPV4 == 1
      s >> ip_address.value.ipv4_value;
#else
      assert(false);
#endif  // AE_SUPPORT_IPV4 == 1
      break;
    case IpAddress::Version::kIpV6:
#if AE_SUPPORT_IPV6 == 1
      s >> ip_address.value.ipv6_value;
#else
      assert(false);
#endif  // AE_SUPPORT_IPV6 == 1
      break;
    default:
      break;
  }
  return s;
}

template <typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, IpAddress const& ip_address) {
  s << ip_address.version;
  switch (ip_address.version) {
    case IpAddress::Version::kIpV4:
#if AE_SUPPORT_IPV4 == 1
      s << ip_address.value.ipv4_value;
#else
      assert(false);
#endif  // AE_SUPPORT_IPV4 == 1
      break;
    case IpAddress::Version::kIpV6:
#if AE_SUPPORT_IPV6 == 1
      s << ip_address.value.ipv6_value;
#else
      assert(false);
#endif  // AE_SUPPORT_IPV6 == 1
      break;
    default:
      break;
  }
  return s;
}

struct IpAddressPort {
  AE_REFLECT_MEMBERS(ip, port)

  IpAddress ip;
  std::uint16_t port;
};

enum class Protocol : std::uint8_t {
  kTcp,
  // TODO: rest does not supported yet
  /*
  kWebSocket,
  kAny,
    kHttp,
    kHttps,
    kUdp, */
};

struct IpAddressPortProtocol : public IpAddressPort {
  AE_REFLECT(AE_REF_BASE(IpAddressPort), AE_MMBR(protocol))

  friend bool operator<(const IpAddressPortProtocol& left,
                        const IpAddressPortProtocol& right);

  Protocol protocol{};
};

#if AE_SUPPORT_CLOUD_DNS
struct NameAddress {
  AE_REFLECT_MEMBERS(name, port, protocol)

  std::string name;
  std::uint16_t port;
  Protocol protocol{};
};
#endif

enum class AddressType : std::uint8_t {
  kResolvedAddress,
  kUnresolvedAddress,
};

struct UnifiedAddress : public VariantType<AddressType, IpAddressPortProtocol
#if AE_SUPPORT_CLOUD_DNS
                                           ,
                                           NameAddress
#endif
                                           > {
  using VariantType::VariantType;
};

template <>
struct Formatter<IpAddress> {
  template <typename TStream>
  void Format(IpAddress const& value, FormatContext<TStream>& ctx) const {
    switch (value.version) {
      case IpAddress::Version::kIpV4: {
#if AE_SUPPORT_IPV4 == 1
        ctx.out().stream() << int{value.value.ipv4_value[0]} << '.'
                           << int{value.value.ipv4_value[1]} << '.'
                           << int{value.value.ipv4_value[2]} << '.'
                           << int{value.value.ipv4_value[3]};
#else
        assert(false);
#endif  // AE_SUPPORT_IPV4 == 1
        break;
      }
      case IpAddress::Version::kIpV6: {
#if AE_SUPPORT_IPV6 == 1
        // TODO: print as conventional IpV6 format
        ctx.out().stream() << std::hex;
        for (std::size_t i = 0; i < 16; i++) {
          ctx.out().stream() << int{value.value.ipv6_value[i]};
          if (i < 15) {
            ctx.out().stream() << ":";
          }
        }
        ctx.out().stream() << std::dec;
#else
        assert(false);
#endif  // AE_SUPPORT_IPV6 == 1
        break;
      }
    }
  }
};

template <>
struct Formatter<IpAddressPort> {
  template <typename TStream>
  void Format(IpAddressPort const& value, FormatContext<TStream>& ctx) const {
    ae::Format(ctx.out(), "{}:{}", value.ip, value.port);
  }
};

template <>
struct Formatter<IpAddressPortProtocol> {
  template <typename TStream>
  void Format(IpAddressPortProtocol const& value,
              FormatContext<TStream>& ctx) const {
    ae::Format(ctx.out(), "{}:{} protocol:{}", value.ip, value.port,
               static_cast<int>(value.protocol));
  }
};

#if AE_SUPPORT_CLOUD_DNS
template <>
struct Formatter<NameAddress> {
  template <typename TStream>
  void Format(NameAddress const& value, FormatContext<TStream>& ctx) const {
    ae::Format(ctx.out(), "{}:{} protocol:{}", value.name, value.port,
               static_cast<int>(value.protocol));
  }
};
#endif

}  // namespace ae

#endif  // AETHER_ADDRESS_H_
