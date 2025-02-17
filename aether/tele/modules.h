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

#ifndef AETHER_TELE_MODULES_H_
#define AETHER_TELE_MODULES_H_

#ifndef AETHER_TELE_TELE_H_
#  error "Include tele.h instead"
#endif

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace ae::tele {
struct Module {
  using underlined_t = std::uint32_t;

  enum _ : underlined_t {
    kNone = 0,
    kApp = 1u << 0,
    kObj = 1u << 1,
    kRegister = 1u << 2,
    kPull = 1u << 3,
    kTransport = 1u << 4,
    kSim = 1u << 30,
    kLog = 1u << 31,
    kAll = 0xFFFFFFFF,
  };

  static std::string text(underlined_t v) {
    using std::string_literals::operator""s;
    using std::string_view_literals::operator""sv;

    if (v == 0) return "kNone"s;
    if ((v & kAll) == kAll) return "kAll"s;

    // extend this list with new modules
    constexpr auto enum_str_pairs = std::array{
        std::pair{kApp, "kApp"sv},           std::pair{kObj, "kObj"sv},
        std::pair{kRegister, "kRegister"sv}, std::pair{kPull, "kPull"sv},
        std::pair{kSim, "kSim"sv},           std::pair{kLog, "kLog"sv},
    };

    std::stringstream ss;
    bool first = true;
    for (auto const& [k, s] : enum_str_pairs) {
      if ((v & k) == k) {
        if (!first) {
          ss << ' ';
        }
        first = false;
        ss << s;
      }
    }
    return ss.str();
  }

  operator underlined_t() const noexcept { return value_; }

  underlined_t value_;
};
}  // namespace ae::tele

#endif  // AETHER_TELE_MODULES_H_
