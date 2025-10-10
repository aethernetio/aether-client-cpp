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

#include "aether/serial_ports/at_comm_support.h"

#include "aether/serial_ports/serial_ports_tele.h"

namespace ae {
UpdateStatus AtCommSupport::WriteAction::Update() {
  // TODO: Is it possible to return Error?
  return UpdateStatus::Result();
}

AtCommSupport::WaitForResponseAction::WaitForResponseAction(
    ActionContext action_context, ISerialPort& serial, std::string expected,
    Duration timeout, Handler response_handler)
    : Action{action_context},
      expected_{std::move(expected)},
      timeout_{timeout},
      success_{},
      error_{},
      response_handler_{std::move(response_handler)} {
  timeout_time_ = Now() + timeout;
  data_read_sub_ = serial.read_event().Subscribe(
      *this, MethodPtr<&AtCommSupport::WaitForResponseAction::DataRead>{});
}

UpdateStatus AtCommSupport::WaitForResponseAction::Update(
    TimePoint current_time) {
  if (success_) {
    return response_handler_(response_buffer_);
  }
  if (error_) {
    return UpdateStatus::Error();
  }
  if (timeout_time_ <= current_time) {
    return UpdateStatus::Error();
  }
  return UpdateStatus::Delay(timeout_time_);
}

void AtCommSupport::WaitForResponseAction::DataRead(DataBuffer const& data) {
  std::string_view response_str(reinterpret_cast<char const*>(data.data()),
                                data.size());
  AE_TELED_DEBUG("AT response: {}", response_str);
  if (response_str.find(expected_) != std::string::npos) {
    success_ = true;
    response_buffer_ = data;
  }
  if (response_str.find("ERROR") != std::string::npos) {
    error_ = true;
  }
  Action::Trigger();
}

AtCommSupport::AtListener::AtListener(ISerialPort& serial, std::string expected,
                                      Handler handler)
    : expected_{std::move(expected)}, handler_{std::move(handler)} {
  data_read_sub_ =
      serial.read_event().Subscribe(*this, MethodPtr<&AtListener::DataRead>{});
}

void AtCommSupport::AtListener::DataRead(DataBuffer const& data) {
  std::string_view response_str(reinterpret_cast<char const*>(data.data()),
                                data.size());
  AE_TELED_DEBUG("AT listen: {}", response_str);
  if (response_str.find(expected_) == std::string::npos) {
    return;
  }

  handler_(data);
}

AtCommSupport::AtCommSupport(ActionContext action_context, ISerialPort& serial)
    : action_context_{action_context}, serial_{&serial} {};

ActionPtr<AtCommSupport::WriteAction> AtCommSupport::SendATCommand(
    std::string const& command) {
  AE_TELED_DEBUG("AT command: {}", command);
  std::vector<uint8_t> data(command.size() + 2);
  data.insert(data.begin(), command.begin(), command.end());
  data.emplace_back('\r');  // Adding a carriage return symbols
  data.emplace_back('\n');
  serial_->Write(data);

  return ActionPtr<WriteAction>{action_context_};
}

ActionPtr<AtCommSupport::WaitForResponseAction> AtCommSupport::WaitForResponse(
    std::string expected, Duration timeout) {
  return ActionPtr<WaitForResponseAction>{action_context_, *serial_,
                                          std::move(expected), timeout};
}

ActionPtr<AtCommSupport::WaitForResponseAction> AtCommSupport::WaitForResponse(
    std::string expected, Duration timeout,
    WaitForResponseAction::Handler response_handler) {
  return ActionPtr<WaitForResponseAction>{action_context_, *serial_,
                                          std::move(expected), timeout,
                                          std::move(response_handler)};
}

std::unique_ptr<AtCommSupport::AtListener> AtCommSupport::ListenForResponse(
    std::string expected, AtListener::Handler handler) {
  return std::make_unique<AtListener>(*serial_, std::move(expected),
                                      std::move(handler));
}

std::string AtCommSupport::PinToString(std::uint16_t pin) {
  static constexpr std::uint16_t kFourNine = 9999;
  if (pin > kFourNine) {
    return "";
  }
  return std::to_string(pin);
}

}  // namespace ae
