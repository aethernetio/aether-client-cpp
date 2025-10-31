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
#include <utility>
#include <string_view>

#include "aether/actions/action.h"
#include "aether/misc/from_chars.h"
#include "aether/types/data_buffer.h"
#include "aether/actions/action_ptr.h"
#include "aether/actions/action_context.h"

#include "aether/serial_ports/iserial_port.h"

// IWYU pragma: begin_exports
#include "aether/serial_ports/at_support/at_buffer.h"
#include "aether/serial_ports/at_support/at_request.h"
#include "aether/serial_ports/at_support/at_listener.h"
#include "aether/serial_ports/at_support/at_dispatcher.h"
#include "aether/serial_ports/at_support/at_write_action.h"
// IWYU pragma: end_exports

namespace ae {
class AtSupport {
 public:
  static std::string PinToString(std::uint16_t pin);

  AtSupport(ActionContext action_context, ISerialPort& serial);

  ActionPtr<AtWriteAction> SendATCommand(std::string const& command);

  template <typename TCommand, typename... Waits>
  ActionPtr<AtRequest> MakeRequest(TCommand&& command, Waits&&... waits) {
    if (!serial_->IsOpen()) {
      return {};
    }
    return ActionPtr<AtRequest>{action_context_, dispatcher_, *this,
                                std::forward<TCommand>(command),
                                std::forward<Waits>(waits)...};
  }

  std::unique_ptr<AtListener> ListenForResponse(std::string expected,
                                                AtListener::Handler handler);

  template <typename... Ts>
  static std::optional<std::size_t> ParseResponse(DataBuffer const& response,
                                                  std::string_view cmd,
                                                  Ts&... args) {
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

 private:
  static bool ParseArg(std::string_view arg, std::string& value) {
    // string value should be in ""
    auto start = arg.find_first_of('"');
    auto end = arg.find_last_of('"');
    if (start == std::string_view::npos || end == std::string_view::npos) {
      return false;
    }
    value = std::string{arg.substr(start + 1, end - start - 1)};
    return true;
  }

  template <typename T>
  static bool ParseArg(std::string_view arg, T& value) {
    auto v = FromChars<T>(arg);
    if (!v.has_value()) {
      return false;
    }
    value = v.value();
    return true;
  }

  ActionContext action_context_;
  ISerialPort* serial_;
  AtBuffer at_buffer_;
  AtDispatcher dispatcher_;
};

} /* namespace ae */

#endif  // AETHER_SERIAL_PORTS_AT_SUPPORT_AT_SUPPORT_H_
