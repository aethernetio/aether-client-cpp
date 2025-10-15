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

AtCommSupport::AtBuffer::AtBuffer(ISerialPort& serial_port)
    : is_open_{serial_port.IsOpen()},
      data_read_sub_{serial_port.read_event().Subscribe(
          *this, MethodPtr<&AtBuffer::DataRead>{})} {}

AtCommSupport::AtBuffer::UpdateEvent::Subscriber
AtCommSupport::AtBuffer::update_event() {
  return EventSubscriber{update_event_};
}

AtCommSupport::AtBuffer::iterator AtCommSupport::AtBuffer::FindPattern(
    std::string_view str) {
  return FindPattern(str, begin());
}

AtCommSupport::AtBuffer::iterator AtCommSupport::AtBuffer::FindPattern(
    std::string_view str, iterator start) {
  auto it = start;
  for (; it != std::end(data_lines_); ++it) {
    std::string_view it_str(reinterpret_cast<char const*>(it->data()),
                            it->size());
    AE_TELED_DEBUG("Searching pattern {} in {}", str, it_str);
    if (it_str.find(str) != std::string::npos) {
      break;
    }
  }

  return it;
}

std::pair<DataBuffer, AtCommSupport::AtBuffer::iterator>
AtCommSupport::AtBuffer::GetCrate(std::size_t size) {
  return GetCrate(size, begin());
}

std::pair<DataBuffer, AtCommSupport::AtBuffer::iterator>
AtCommSupport::AtBuffer::GetCrate(std::size_t size, iterator start) {
  DataBuffer res;
  auto it = start;
  for (; it != std::end(data_lines_) && res.size() < size; it++) {
    res.insert(std::end(res), it->begin(), it->end());
  }

  return {std::move(res), it};
}

AtCommSupport::AtBuffer::iterator AtCommSupport::AtBuffer::begin() {
  return std::begin(data_lines_);
}

AtCommSupport::AtBuffer::iterator AtCommSupport::AtBuffer::end() {
  return std::end(data_lines_);
}

AtCommSupport::AtBuffer::iterator AtCommSupport::AtBuffer::erase(iterator it) {
  return data_lines_.erase(it);
}

AtCommSupport::AtBuffer::iterator AtCommSupport::AtBuffer::erase(
    iterator first, iterator last) {
  return data_lines_.erase(first, last);
}

bool AtCommSupport::AtBuffer::IsOpen() const { return is_open_; }

void AtCommSupport::AtBuffer::DataRead(DataBuffer const& data) {
  AE_TELED_DEBUG("AtBuffer receives packet {}", data);
  auto data_str =
      std::string_view{reinterpret_cast<const char*>(data.data()), data.size()};
  auto start = std::end(data_lines_);
  while (!data_str.empty()) {
    auto line_end = data_str.find('\n');
    assert(line_end != 0);
    // skip \r\n
    auto sub = data_str.substr(0, line_end - 1);
    if (sub.empty()) {
      continue;
    }
    AE_TELED_DEBUG("AtBuffer adds line {}", sub);
    auto it = data_lines_.emplace(
        std::end(data_lines_),
        reinterpret_cast<std::uint8_t const*>(sub.data()),
        reinterpret_cast<std::uint8_t const*>(sub.data()) + sub.size());

    if (start == std::end(data_lines_)) {
      start = it;
    }

    data_str = data_str.substr(line_end + 1);
  }
  update_event_.Emit(start);
}

AtCommSupport::WriteAction::WriteAction(ActionContext action_context,
                                        bool is_write_success)
    : Action{action_context}, is_write_success_{is_write_success} {}

UpdateStatus AtCommSupport::WriteAction::Update() const {
  if (is_write_success_) {
    return UpdateStatus::Result();
  }
  return UpdateStatus::Error();
}

AtCommSupport::WaitForResponseAction::WaitForResponseAction(
    ActionContext action_context, AtBuffer& buffer, std::string expected,
    Duration timeout, Handler response_handler)
    : Action{action_context},
      expected_{std::move(expected)},
      timeout_{timeout},
      buffer_{&buffer},
      success_{},
      error_{!buffer_->IsOpen()},
      response_handler_{std::move(response_handler)} {
  timeout_time_ = Now() + timeout;

  // check buffer for pattern or subscribe to new updates
  auto res = buffer_->FindPattern(expected_);
  if (res != std::end(*buffer_)) {
    response_pos_ = res;
    success_ = true;
  } else {
    data_update_sub_ = buffer_->update_event().Subscribe(
        *this, MethodPtr<&AtCommSupport::WaitForResponseAction::DataRead>{});
  }
}

UpdateStatus AtCommSupport::WaitForResponseAction::Update(
    TimePoint current_time) {
  if (success_) {
    return response_handler_(*buffer_, response_pos_);
  }
  if (error_) {
    return UpdateStatus::Error();
  }
  if (timeout_time_ <= current_time) {
    return UpdateStatus::Error();
  }
  return UpdateStatus::Delay(timeout_time_);
}

void AtCommSupport::WaitForResponseAction::DataRead(AtBuffer::iterator pos) {
  auto res = buffer_->FindPattern(expected_, pos);
  if (res != std::end(*buffer_)) {
    auto res_str = std::string_view{reinterpret_cast<char const*>(res->data()),
                                    res->size()};
    AE_TELED_DEBUG("Received response: {}", res_str);
    success_ = true;
    response_pos_ = res;
    data_update_sub_.Reset();
    Action::Trigger();
    return;
  }
  res = buffer_->FindPattern("ERROR", pos);
  if (res != std::end(*buffer_)) {
    buffer_->erase(res);
    error_ = true;
    data_update_sub_.Reset();
    Action::Trigger();
    return;
  }
}

AtCommSupport::AtListener::AtListener(AtBuffer& buffer, std::string expected,
                                      Handler handler)
    : expected_{std::move(expected)},
      buffer_{&buffer},
      handler_{std::move(handler)} {
  // check all the buffer first
  auto res = buffer_->FindPattern(expected_);
  for (; res != std::end(*buffer_);
       res = buffer_->FindPattern(expected_, res)) {
    res = handler_(*buffer_, res);
  }

  // subscribe to new data updates
  data_update_sub_ = buffer_->update_event().Subscribe(
      *this, MethodPtr<&AtListener::DataRead>{});
}

void AtCommSupport::AtListener::DataRead(AtBuffer::iterator it) {
  if (auto res = buffer_->FindPattern(expected_, it);
      res != std::end(*buffer_)) {
    handler_(*buffer_, res);
  }
}

AtCommSupport::AtCommSupport(ActionContext action_context, ISerialPort& serial)
    : action_context_{action_context}, serial_{&serial}, at_buffer_{serial} {};

ActionPtr<AtCommSupport::WriteAction> AtCommSupport::SendATCommand(
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

ActionPtr<AtCommSupport::WaitForResponseAction> AtCommSupport::WaitForResponse(
    std::string expected, Duration timeout) {
  return ActionPtr<WaitForResponseAction>{action_context_, at_buffer_,
                                          std::move(expected), timeout};
}

ActionPtr<AtCommSupport::WaitForResponseAction> AtCommSupport::WaitForResponse(
    std::string expected, Duration timeout,
    WaitForResponseAction::Handler response_handler) {
  return ActionPtr<WaitForResponseAction>{action_context_, at_buffer_,
                                          std::move(expected), timeout,
                                          std::move(response_handler)};
}

std::unique_ptr<AtCommSupport::AtListener> AtCommSupport::ListenForResponse(
    std::string expected, AtListener::Handler handler) {
  return std::make_unique<AtListener>(at_buffer_, std::move(expected),
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
