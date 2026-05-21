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

#include "aether/serial_ports/at_support/at_support.h"

#include "aether/serial_ports/serial_ports_tele.h"

namespace ae {
namespace at_support {
std::string PinToString(std::uint16_t pin) {
  static constexpr std::uint16_t kFourNine = 9999;
  if (pin > kFourNine) {
    return "";
  }
  return std::to_string(pin);
}

bool ParseArg(std::string_view arg, std::string& value) {
  // string value should be in ""
  auto start = arg.find_first_of('"');
  auto end = arg.find_last_of('"');
  if (start == std::string_view::npos || end == std::string_view::npos) {
    return false;
  }
  value = std::string{arg.substr(start + 1, end - start - 1)};
  return true;
}

}  // namespace at_support

AtSupport::AtSupport(ISerialPort& serial) noexcept
    : serial_{&serial}, at_buffer_{serial}, dispatcher_{at_buffer_} {};

Result<std::size_t, int> AtSupport::SendATCommand(std::string_view command) {
  if (!serial_->IsOpen()) {
    return Error{-1};
  }

  AE_TELED_DEBUG("AT command: {}", command);

  static std::array<std::uint8_t, 1024> buffer;
  assert(command.size() + 2 < buffer.size());

  std::copy(command.begin(), command.end(), buffer.begin());
  buffer[command.size()] = '\r';
  buffer[command.size() + 1] = '\n';

  // TODO: add serial error
  serial_->Write(buffer);

  return Ok{command.size()};
}

AtBuffer& AtSupport::at_buffer() { return at_buffer_; }
AtDispatcher& AtSupport::dispatcher() { return dispatcher_; }

}  // namespace ae
