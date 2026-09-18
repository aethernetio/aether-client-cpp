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

#include <charconv>

#include "aether/tele.h"

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
  auto remaining_size = static_cast<std::ptrdiff_t>(size);
  for (auto it = start; it != std::end(data_lines_) && res.size() < size;
       it++) {
    if (copy_offset >= it->size()) {
      copy_offset -= it->size();
      continue;
    }
    auto first = it->begin() + static_cast<std::ptrdiff_t>(copy_offset);
    auto bucket_size = it->end() - first;
    auto last =
        first + ((remaining_size < bucket_size) ? remaining_size : bucket_size);

    res.insert(std::end(res), first, last);
    remaining_size -= last - first;
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

void AtBuffer::DataRead(std::span<std::uint8_t const> data) {
  AE_TELED_DEBUG("AtBuffer receives packet {}", data);
  pending_data_.insert(std::end(pending_data_), std::begin(data),
                       std::end(data));

  auto start = std::end(data_lines_);
  auto add_line = [&](std::string_view line) {
    if (line.empty()) {
      return;
    }

    AE_TELED_DEBUG("AtBuffer adds line {}", line);
    auto it = data_lines_.emplace(
        std::end(data_lines_),
        reinterpret_cast<std::uint8_t const*>(line.data()),
        reinterpret_cast<std::uint8_t const*>(line.data()) + line.size());
    if (start == std::end(data_lines_)) {
      start = it;
    }
  };

  while (!pending_data_.empty()) {
    auto data_str =
        std::string_view{reinterpret_cast<char const*>(pending_data_.data()),
                         pending_data_.size()};

    if (data_str.starts_with("\r\n")) {
      pending_data_.erase(std::begin(pending_data_),
                          std::begin(pending_data_) + 2);
      continue;
    }

    // +CARECV carries arbitrary binary data. Its payload may contain CR/LF,
    // therefore frame it using the declared byte count instead of looking for
    // the first line terminator.
    static constexpr auto kCarecvPrefix = std::string_view{"+CARECV: "};
    if (data_str.starts_with(kCarecvPrefix)) {
      auto comma = data_str.find(',', kCarecvPrefix.size());
      auto line_end = data_str.find("\r\n", kCarecvPrefix.size());
      if (comma != std::string_view::npos &&
          (line_end == std::string_view::npos || comma < line_end)) {
        std::size_t payload_size{};
        auto size_begin = data_str.data() + kCarecvPrefix.size();
        auto size_end = data_str.data() + comma;
        auto [parse_end, parse_error] =
            std::from_chars(size_begin, size_end, payload_size);
        if (parse_error == std::errc{} && parse_end == size_end) {
          auto frame_size = comma + 1 + payload_size;
          auto response_size = frame_size + 2;
          if (data_str.size() < response_size) {
            break;
          }
          if (data_str.substr(frame_size, 2) == "\r\n") {
            add_line(data_str.substr(0, frame_size));
            pending_data_.erase(std::begin(pending_data_),
                                std::begin(pending_data_) +
                                    static_cast<std::ptrdiff_t>(response_size));
            continue;
          }
        }
      }
    }

    auto line_end = data_str.find("\r\n");
    if (line_end == std::string_view::npos) {
      // The SIM7070 data-entry prompt is not terminated by CR/LF.
      if (data_str == ">" || data_str == "> ") {
        add_line(data_str);
        pending_data_.clear();
      }
      break;
    }

    add_line(data_str.substr(0, line_end));
    pending_data_.erase(
        std::begin(pending_data_),
        std::begin(pending_data_) + static_cast<std::ptrdiff_t>(line_end + 2));
  }

  if (start != std::end(data_lines_)) {
    update_event_.Emit(start);
  }
}
}  // namespace ae
