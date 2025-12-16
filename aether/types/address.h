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

#ifndef AETHER_TYPES_ADDRESS_H_
#define AETHER_TYPES_ADDRESS_H_

#include <cstdint>
#include <string>

#include "aether/config.h"
#include "aether/reflect/reflect.h"
#include "aether/types/variant_type.h"

#include "aether/format/format.h"

namespace ae {

enum class AddrVersion : std::uint8_t {
  kNull = 0,
  kIpV4 = 1,
  kIpV6 = 2,
  kNamed = 3,
};

struct IpV4Addr {
  std::uint8_t ipv4_value[4];
};

bool operator==(IpV4Addr const& left, IpV4Addr const& right);
bool operator!=(IpV4Addr const& left, IpV4Addr const& right);
bool operator<(IpV4Addr const& left, IpV4Addr const& right);

template <typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& s, IpV4Addr& ipv4) {
  s >> ipv4.ipv4_value;
  return s;
}

template <typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, IpV4Addr const& ipv4) {
  s << ipv4.ipv4_value;
  return s;
}

struct IpV6Addr {
  std::uint8_t ipv6_value[16];
};

bool operator==(IpV6Addr const& left, IpV6Addr const& right);
bool operator!=(IpV6Addr const& left, IpV6Addr const& right);
bool operator<(IpV6Addr const& left, IpV6Addr const& right);

template <typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& s, IpV6Addr& ipv6) {
  s >> ipv6.ipv6_value;
  return s;
}

template <typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, IpV6Addr const& ipv6) {
  s << ipv6.ipv6_value;
  return s;
}

struct NamedAddr {
  AE_REFLECT_MEMBERS(name)
  std::string name;
};

bool operator==(NamedAddr const& left, NamedAddr const& right);
bool operator!=(NamedAddr const& left, NamedAddr const& right);
bool operator<(NamedAddr const& left, NamedAddr const& right);

struct Address
    : public VariantType<AddrVersion, VPair<AddrVersion::kIpV4, IpV4Addr>,
                         VPair<AddrVersion::kIpV6, IpV6Addr>,
                         VPair<AddrVersion::kNamed, NamedAddr>> {
  using VariantType::VariantType;
  using VariantType::operator=;
};

bool operator==(Address const& left, Address const& right);
bool operator!=(Address const& left, Address const& right);
bool operator<(Address const& left, Address const& right);

struct AddressPort {
  AE_REFLECT_MEMBERS(address, port)

  Address address;
  std::uint16_t port;
};

enum class Protocol : std::uint8_t {
  kTcp,
  kUdp,
  kWebSocket,  // does not supported really
  // TODO: rest does not supported yet
  /*
  kAny,
  kHttp,
  kHttps, */
};

struct Endpoint : public AddressPort {
  AE_REFLECT(AE_REF_BASE(AddressPort), AE_MMBR(protocol))

  friend bool operator<(const Endpoint& left, const Endpoint& right);

  Protocol protocol{};
};

bool operator<(Endpoint const& left, Endpoint const& right);

template <>
struct Formatter<IpV4Addr> {
  template <typename TStream>
  void Format([[maybe_unused]] IpV4Addr const& value,
              [[maybe_unused]] FormatContext<TStream>& ctx) const {
#if AE_SUPPORT_IPV4 == 1
    ae::Format(ctx.out(), "{}.{}.{}.{}", static_cast<int>(value.ipv4_value[0]),
               static_cast<int>(value.ipv4_value[1]),
               static_cast<int>(value.ipv4_value[2]),
               static_cast<int>(value.ipv4_value[3]));
#endif
  }
};

template <>
struct Formatter<IpV6Addr> {
  template <typename TStream>
  void Format([[maybe_unused]] IpV6Addr const& value,
              [[maybe_unused]] FormatContext<TStream>& ctx) const {
#if AE_SUPPORT_IPV6 == 1
    ctx.out().stream() << std::hex;
    for (std::size_t i = 0; i < 16; i++) {
      ctx.out().stream() << static_cast<int>(value.ipv6_value[i]);
      if (i < 15) {
        ctx.out().stream() << ':';
      }
    }
    ctx.out().stream() << std::dec;
#endif
  }
};

template <>
struct Formatter<NamedAddr> {
  template <typename TStream>
  void Format([[maybe_unused]] NamedAddr const& value,
              [[maybe_unused]] FormatContext<TStream>& ctx) const {
#if AE_SUPPORT_CLOUD_DNS == 1
    ctx.out().stream() << value.name;
#endif
  }
};

template <>
struct Formatter<Address> {
  template <typename TStream>
  void Format(Address const& value, FormatContext<TStream>& ctx) const {
    std::visit([&](auto const& addr) { ae::Format(ctx.out(), "{}", addr); },
               value);
  }
};

template <>
struct Formatter<AddressPort> {
  template <typename TStream>
  void Format(AddressPort const& value, FormatContext<TStream>& ctx) const {
    ae::Format(ctx.out(), "{}:{}", value.address, value.port);
  }
};

template <>
struct Formatter<Endpoint> {
  template <typename TStream>
  void Format(Endpoint const& value, FormatContext<TStream>& ctx) const {
    ae::Format(ctx.out(), "{}:{} protocol:{}", value.address, value.port,
               static_cast<int>(value.protocol));
  }
};
}  // namespace ae

#endif  // AETHER_TYPES_ADDRESS_H_
