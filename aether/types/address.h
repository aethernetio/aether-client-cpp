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

#include <charconv>  // IWYU pragma: keep
#include <cstddef>   // IWYU pragma: keep
#include <cstdint>
#include <string>
#include <string_view>  // IWYU pragma: keep
#include <type_traits>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-miscpp/serialization/serialization.h"

#include "aether/config.h"
#include "aether/types/variant_type.h"

#include "aether-miscpp/format/format.h"

namespace ae {

enum class AddrVersion : std::uint8_t {
  kNull = 0,
  kIpV4 = 1,
  kIpV6 = 2,
  kNamed = 3,
  kBrowser = 4,
};

struct IpV4Addr {
  std::uint8_t ipv4_value[4];
};

bool operator==(IpV4Addr const& left, IpV4Addr const& right);
bool operator!=(IpV4Addr const& left, IpV4Addr const& right);
bool operator<(IpV4Addr const& left, IpV4Addr const& right);

namespace seri {
template <Archive A>
struct Serializer<A, IpV4Addr> {
  SeriResult Seri(A& archive, Meta<IpV4Addr const> meta) const {
    return archive.Save(
        Meta<std::uint8_t const[4]>{meta.value.ipv4_value, "value"});
  }
  SeriResult Deseri(A& archive, Meta<IpV4Addr> meta) const {
    return archive.Load(Meta<std::uint8_t[4]>{meta.value.ipv4_value, "value"});
  }
};
}  // namespace seri

struct IpV6Addr {
  std::uint8_t ipv6_value[16];
};

bool operator==(IpV6Addr const& left, IpV6Addr const& right);
bool operator!=(IpV6Addr const& left, IpV6Addr const& right);
bool operator<(IpV6Addr const& left, IpV6Addr const& right);

namespace seri {
template <Archive A>
struct Serializer<A, IpV6Addr> {
  SeriResult Seri(A& archive, Meta<IpV6Addr const> meta) const {
    return archive.Save(
        Meta<std::uint8_t const[16]>{meta.value.ipv6_value, "value"});
  }
  SeriResult Deseri(A& archive, Meta<IpV6Addr> meta) const {
    return archive.Load(Meta<std::uint8_t[16]>{meta.value.ipv6_value, "value"});
  }
};
}  // namespace seri

struct NamedAddr {
  AE_REFLECT_MEMBERS(name)

  std::string name;
};

bool operator==(NamedAddr const& left, NamedAddr const& right);
bool operator!=(NamedAddr const& left, NamedAddr const& right);
bool operator<(NamedAddr const& left, NamedAddr const& right);

struct BrowserAddr {
  AE_REFLECT_MEMBERS(representation_version, hostname, path, gateway_target)

  std::uint8_t representation_version{1};
  std::string hostname;
  std::string path;
  std::string gateway_target;
};

bool operator==(BrowserAddr const& left, BrowserAddr const& right);
bool operator!=(BrowserAddr const& left, BrowserAddr const& right);
bool operator<(BrowserAddr const& left, BrowserAddr const& right);

struct Address
    : public VariantType<AddrVersion, VPair<AddrVersion::kIpV4, IpV4Addr>,
                         VPair<AddrVersion::kIpV6, IpV6Addr>,
                         VPair<AddrVersion::kNamed, NamedAddr>,
                         VPair<AddrVersion::kBrowser, BrowserAddr>> {
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

// Wire ordinals must match Java AetherCodec in common.adsl.yaml:
// TCP=0, UDP=1, WS=2, WSS=3. HTTP/HTTPS are client-local tunnel codecs
// appended after the shared wire values (not advertised by production cloud).
enum class Protocol : std::uint8_t {
  kTcp = 0,
  kUdp = 1,
  kWebSocket = 2,         // insecure ws:// (Java WS)
  kWebSocketSecure = 3,   // secure wss:// (Java WSS)
  kHttp = 4,              // insecure HTTP tunnel (browser gateway)
  kHttps = 5,             // secure HTTP tunnel (browser gateway)
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
    Formatter<int>{}.Format(static_cast<int>(value.ipv4_value[0]), ctx);
    ctx.out().write('.');
    Formatter<int>{}.Format(static_cast<int>(value.ipv4_value[1]), ctx);
    ctx.out().write('.');
    Formatter<int>{}.Format(static_cast<int>(value.ipv4_value[2]), ctx);
    ctx.out().write('.');
    Formatter<int>{}.Format(static_cast<int>(value.ipv4_value[3]), ctx);
#endif
  }
};

template <>
struct Formatter<IpV6Addr> {
  template <typename TStream>
  void Format([[maybe_unused]] IpV6Addr const& value,
              [[maybe_unused]] FormatContext<TStream>& ctx) const {
#if AE_SUPPORT_IPV6 == 1
    char buffer[2]{};
    for (std::size_t i = 0; i < 16; i++) {
      auto result =
          std::to_chars(buffer, buffer + 2,
                        static_cast<unsigned int>(value.ipv6_value[i]), 16);
      ctx.out().write(std::string_view{
          buffer, static_cast<std::size_t>(result.ptr - buffer)});
      if (i < 15) {
        ctx.out().write(':');
      }
    }
#endif
  }
};

template <>
struct Formatter<NamedAddr> {
  template <typename TStream>
  void Format([[maybe_unused]] NamedAddr const& value,
              [[maybe_unused]] FormatContext<TStream>& ctx) const {
#if AE_SUPPORT_CLOUD_DNS == 1
    Formatter<std::string>{}.Format(value.name, ctx);
#endif
  }
};

template <>
struct Formatter<BrowserAddr> {
  template <typename TStream>
  void Format(BrowserAddr const& value, FormatContext<TStream>& ctx) const {
    Formatter<std::string>{}.Format(value.hostname, ctx);
    ctx.out().write(value.path);
    if (!value.gateway_target.empty()) {
      ctx.out().write("?target=");
      Formatter<std::string>{}.Format(value.gateway_target, ctx);
    }
  }
};

template <>
struct Formatter<Address> {
  template <typename TStream>
  void Format(Address const& value, FormatContext<TStream>& ctx) const {
    std::visit(
        [&](auto const& addr) {
          using Addr = std::decay_t<decltype(addr)>;
          Formatter<Addr>{}.Format(addr, ctx);
        },
        value);
  }
};

template <>
struct Formatter<AddressPort> {
  template <typename TStream>
  void Format(AddressPort const& value, FormatContext<TStream>& ctx) const {
    Formatter<Address>{}.Format(value.address, ctx);
    ctx.out().write(':');
    Formatter<std::uint16_t>{}.Format(value.port, ctx);
  }
};

template <>
struct Formatter<Endpoint> {
  template <typename TStream>
  void Format(Endpoint const& value, FormatContext<TStream>& ctx) const {
    Formatter<Address>{}.Format(value.address, ctx);
    ctx.out().write(':');
    Formatter<std::uint16_t>{}.Format(value.port, ctx);
    ctx.out().write(" protocol:");
    Formatter<int>{}.Format(static_cast<int>(value.protocol), ctx);
  }
};
}  // namespace ae

#endif  // AETHER_TYPES_ADDRESS_H_
