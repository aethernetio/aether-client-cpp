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

#include <vector>
#include <string_view>

#include "aether/serial_ports/at_support/at_support.h"

#include "aether/serial_ports/serial_ports_tele.h"

namespace ae {
AtSupport::WriteAction::WriteAction(ActionContext action_context,
                                    bool is_write_success)
    : Action{action_context}, is_write_success_{is_write_success} {}

UpdateStatus AtSupport::WriteAction::Update() const {
  if (is_write_success_) {
    return UpdateStatus::Result();
  }
  return UpdateStatus::Error();
}

AtSupport::AtSupport(ActionContext action_context, ISerialPort& serial)
    : action_context_{action_context},
      serial_{&serial},
      at_buffer_{serial},
      dispatcher_{at_buffer_} {};

ActionPtr<AtSupport::WriteAction> AtSupport::SendATCommand(
    std::string const& command) {
  if (!serial_->IsOpen()) {
    return ActionPtr<WriteAction>{action_context_, false};
  }

  AE_TELED_DEBUG("AT command: {}", command);
  std::vector<uint8_t> data(command.size() + 2);
  std::copy(command.begin(), command.end(), data.begin());
  data[command.size()] = '\r';
  data[command.size() + 1] = '\n';
  serial_->Write(data);

  return ActionPtr<WriteAction>{action_context_, true};
}

std::unique_ptr<AtListener> AtSupport::ListenForResponse(
    std::string expected, AtListener::Handler handler) {
  return std::make_unique<AtListener>(dispatcher_, std::move(expected),
                                      std::move(handler));
}

std::string AtSupport::PinToString(std::uint16_t pin) {
  static constexpr std::uint16_t kFourNine = 9999;
  if (pin > kFourNine) {
    return "";
  }
  return std::to_string(pin);
}

}  // namespace ae
