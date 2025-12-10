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

#include <cctype>
#include <string_view>

#include "aether/types/address.h"
#include "aether/types/literal_array.h"

namespace ae {

class AddressParser {
 public:
  struct NamedAddrView {
    std::string_view name;

    operator NamedAddr() const { return NamedAddr{std::string{name}}; }
  };

  struct ParsedAddress
      : public VariantType<AddrVersion, VPair<AddrVersion::kIpV4, IpV4Addr>,
                           VPair<AddrVersion::kIpV6, IpV6Addr>,
                           VPair<AddrVersion::kNamed, NamedAddrView>> {
    using VariantType::VariantType;
    using VariantType::operator=;

    operator Address() const {
      return std::visit([](auto const& arg) { return Address{arg}; }, *this);
    }
  };

  static constexpr ParsedAddress StringToAddress(std::string_view addr_string) {
    if (auto a = IpV4Parse(addr_string); a) {
      return a.value();
    }
    if (auto a = IpV6Parse(addr_string); a) {
      return a.value();
    }
    return NamedAddrView{addr_string};
  }

 private:
  static constexpr std::optional<IpV4Addr> IpV4Parse(std::string_view addr) {
    IpV4Addr a{};
    std::size_t triplets_i = 0;
    for (std::size_t i = 0; i < addr.size(); ++i) {
      if (addr[i] == '\n') {
        break;
      }
      if (addr[i] == '.') {
        triplets_i++;
        if (triplets_i >= 4) {
          return std::nullopt;
        }
        continue;
      }
      if (!IsDigit(addr[i])) {
        return std::nullopt;
      }
      a.ipv4_value[triplets_i] = a.ipv4_value[triplets_i] * 10 +
                                 static_cast<std::uint8_t>(addr[i] - '0');
    }
    return a;
  }

  static constexpr std::optional<IpV6Addr> IpV6Parse(std::string_view addr) {
    std::array<std::uint16_t, 8> segments{};
    std::size_t seg_index = 0;
    std::size_t compress_pos = 0;
    bool has_compression = false;

    for (std::size_t i = 0; i < addr.size(); ++i) {
      if (seg_index > 8) {
        return std::nullopt;
      }
      if (addr[i] == '\n') {
        break;
      }

      if (addr[i] == ':') {
        // check for ::
        if (((i + 1) < addr.size()) && addr[i + 1] == ':') {
          // multiple compression does not allowed
          if (has_compression) {
            return std::nullopt;
          }
          has_compression = true;
          compress_pos = seg_index;
          i++;
        } else {
          // single colon
          if (seg_index == 0) {
            // should not start with a single colon
            return std::nullopt;
          }
        }
        continue;
      }

      // parse hex segment
      std::uint16_t value = 0;
      int digits = 0;
      for (; i < addr.size(); ++i) {
        if ((addr[i] == ':') || (addr[i] == '\n')) {
          // step back and finish this segment
          --i;
          break;
        }
        if (!IsXDigit(addr[i])) {
          return std::nullopt;
        }

        auto h = _internal::FromHex(addr[i]);
        value = static_cast<std::uint16_t>(value << 4) |
                static_cast<std::uint16_t>(h);
        digits++;
        if (digits >= 4) {
          break;
        }
      }
      segments[seg_index++] = value;
    }

    // apply compression
    if (has_compression) {
      std::size_t total_segs = seg_index;
      std::size_t shift = 8 - total_segs + (compress_pos < total_segs ? 0 : 1);

      // Shift segments to the right
      for (std::size_t j = total_segs; j > compress_pos; j--) {
        if ((j - 1 >= 8) || (j + shift - 1 >= 8)) {
          return {};
        }
        segments[j + shift - 1] = segments[j - 1];
        segments[j - 1] = 0;
      }
    }
    // make IpV6Addr
    IpV6Addr a{};
    for (std::size_t i = 0; i < 16; i++) {
      a.ipv6_value[i] = segments.data()[i / 2] >> ((1 - i % 2) * 8) & 0xFF;
    }
    return a;
  }

  static constexpr bool IsDigit(char c) { return c >= '0' && c <= '9'; }
  static constexpr bool IsXDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
  }
};
}  // namespace ae
#endif  // AETHER_TYPES_ADDRESS_PARSER_H_
