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

#include "aether/serial_ports/at_support/at_buffer.h"

#include "aether/tele/tele.h"

namespace ae {
AtBuffer::AtBuffer(ISerialPort& serial_port)
    : data_read_sub_{serial_port.read_event().Subscribe(
          MethodPtr<&AtBuffer::DataRead>{this})} {}

AtBuffer::UpdateEvent::Subscriber AtBuffer::update_event() {
  return EventSubscriber{update_event_};
}

AtBuffer::iterator AtBuffer::FindPattern(std::string_view str) {
  return FindPattern(str, begin());
}

AtBuffer::iterator AtBuffer::FindPattern(std::string_view str, iterator start) {
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

DataBuffer AtBuffer::GetCrate(std::size_t size, std::size_t offset) {
  return GetCrate(size, offset, begin());
}

DataBuffer AtBuffer::GetCrate(std::size_t size, std::size_t offset,
                              iterator start) {
  DataBuffer res;
  auto copy_offset = offset;
  auto remaining_size = size;
  for (auto it = start; it != std::end(data_lines_) && res.size() < size;
       it++) {
    if (copy_offset >= it->size()) {
      copy_offset -= it->size();
      continue;
    }
    auto first = it->begin() + static_cast<std::ptrdiff_t>(copy_offset);
    auto last = first + static_cast<std::ptrdiff_t>(remaining_size);
    if (last > it->end()) {
      last = it->end();
    }

    res.insert(std::end(res), first, last);
    remaining_size -= static_cast<std::size_t>(last - first);
    copy_offset = 0;
  }

  return res;
}

AtBuffer::iterator AtBuffer::begin() { return std::begin(data_lines_); }

AtBuffer::iterator AtBuffer::end() { return std::end(data_lines_); }

AtBuffer::iterator AtBuffer::erase(iterator it) {
  return data_lines_.erase(it);
}

AtBuffer::iterator AtBuffer::erase(iterator first, iterator last) {
  return data_lines_.erase(first, last);
}

void AtBuffer::DataRead(DataBuffer const& data) {
  AE_TELED_DEBUG("AtBuffer receives packet {}", data);
  auto data_str =
      std::string_view{reinterpret_cast<const char*>(data.data()), data.size()};
  auto start = std::end(data_lines_);
  while (!data_str.empty()) {
    auto line_end = data_str.find("\r\n");
    if (line_end == std::string_view::npos) {
      AE_TELED_ERROR("The line without \\r\\n {}", data_str);
      line_end = data_str.size();
    }
    // skip \r\n
    auto sub = data_str.substr(0, line_end);
    // move to next line
    data_str.remove_prefix((line_end == data_str.size()) ? data_str.size()
                                                         : line_end + 2);

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
  }
  update_event_.Emit(start);
}
}  // namespace ae
