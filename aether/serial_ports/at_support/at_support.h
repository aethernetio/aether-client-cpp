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

#ifndef AETHER_SERIAL_PORTS_AT_SUPPORT_AT_SUPPORT_H_
#define AETHER_SERIAL_PORTS_AT_SUPPORT_AT_SUPPORT_H_

#include <string>
#include <cstddef>
#include <optional>
#include <string_view>

#include "aether/types/result.h"
#include "aether/misc/from_chars.h"

#include "aether/serial_ports/iserial_port.h"

// IWYU pragma: begin_exports
#include "aether/serial_ports/at_support/at_buffer.h"
#include "aether/serial_ports/at_support/at_listener.h"
#include "aether/serial_ports/at_support/at_dispatcher.h"
#include "aether/serial_ports/at_support/at_write_action.h"
// IWYU pragma: end_exports

namespace ae {
namespace at_support {
std::string PinToString(std::uint16_t pin);

bool ParseArg(std::string_view arg, std::string& value);

template <typename T>
bool ParseArg(std::string_view arg, T& value) {
  auto v = FromChars<T>(arg);
  if (!v.has_value()) {
    return false;
  }
  value = v.value();
  return true;
}

template <typename... Ts>
static std::optional<std::size_t> ParseResponse(
    std::span<std::uint8_t const> response, std::string_view cmd, Ts&... args) {
  // response must be a valid ascii string
  auto resp_str = std::string_view{
      reinterpret_cast<char const*>(response.data()), response.size()};

  auto start = resp_str.find(cmd);
  if (start == std::string_view::npos) {
    return false;
  }
  start += cmd.size() + 2;  // 2 for ": "
  auto end = start;
  bool res = true;
  (std::invoke([&]() {
     if (start >= resp_str.size()) {
       res = false;
       return;
     }
     end = resp_str.find_first_of(", \n\r", start);
     if (end == std::string_view::npos) {
       end = resp_str.size();
     }
     if (end <= start) {
       res = false;
       return;
     }
     res = res && ParseArg(resp_str.substr(start, end - start), args);
     start = end + 1;
   }),
   ...);
  return res ? std::optional{end} : std::nullopt;
}

}  // namespace at_support

class AtSupport {
 public:
  explicit AtSupport(ISerialPort& serial) noexcept;

  Result<std::size_t, int> SendATCommand(std::string_view command);

  AtListener ListenForResponse(std::string expected,
                               AtListener::Handler handler);

  AtBuffer& at_buffer();
  AtDispatcher& dispatcher();

 private:
  ISerialPort* serial_;
  AtBuffer at_buffer_;
  AtDispatcher dispatcher_;
};
} /* namespace ae */

#endif  // AETHER_SERIAL_PORTS_AT_SUPPORT_AT_SUPPORT_H_
