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

#ifndef AETHER_POLLER_POLLER_TYPES_H_
#define AETHER_POLLER_POLLER_TYPES_H_

#include <cstdint>

#include "aether/format/format.h"

namespace ae {
struct EventType {
  static constexpr std::uint8_t kRead = 0x1;
  static constexpr std::uint8_t kWrite = 0x2;
  static constexpr std::uint8_t kError = 0x4;

  EventType() = default;
  EventType(std::uint8_t v) : value{v} {}

  std::uint8_t operator&(std::uint8_t other) const { return value & other; }
  std::uint8_t operator|(std::uint8_t other) const { return value | other; }
  EventType operator|=(std::uint8_t other) { return EventType{value |= other}; }

  bool operator==(EventType ev) const { return value == ev.value; }
  bool operator!=(EventType ev) const { return value != ev.value; }

  explicit operator std::uint8_t() const { return value; }

  std::uint8_t value;
};

struct DescriptorType {
  // Add our own defines to prevent windows.h in public header
#if defined _WIN32
  using Handle = void*;
#  if defined _WIN64
  using Socket = std::uint64_t;
#  else
  using Socket = std::uint32_t;
#  endif

  DescriptorType(Handle des) : descriptor{des} {}
  DescriptorType(Socket des) : descriptor{reinterpret_cast<Handle>(des)} {}

  explicit operator Handle() const { return descriptor; }
  explicit operator Socket() const {
    return reinterpret_cast<Socket>(descriptor);
  }

  bool operator<(DescriptorType const& other) const {
    return descriptor < other.descriptor;
  }

  bool operator==(DescriptorType const& other) const {
    return descriptor == other.descriptor;
  }

  bool operator!=(DescriptorType const& other) const {
    return descriptor != other.descriptor;
  }

  Handle descriptor;
#else
  DescriptorType(int des) : descriptor{des} {}

  operator int() const { return descriptor; }

  int descriptor;
#endif
};

template <>
struct Formatter<EventType> {
  template <typename TStream>
  void Format(EventType const& value, FormatContext<TStream>& ctx) {
    int index = 0;
    for (auto e : {EventType::kRead, EventType::kWrite, EventType::kError}) {
      if (auto v = (value & e); v != 0) {
        auto str_value = std::invoke(
            [](auto value, int flag) {
              switch (value) {
                case EventType::kRead:
                  return !flag ? std::string_view{"kRead"}
                               : std::string_view{" | kRead"};
                case EventType::kWrite:
                  return !flag ? std::string_view{"kWrite"}
                               : std::string_view{" | kWrite"};
                case EventType::kError:
                  return !flag ? std::string_view{"kError"}
                               : std::string_view{" | kError"};
                default:
                  break;
              }
              return std::string_view{"UNKNOWN"};
            },
            v, index++);
        ctx.out().write(str_value);
      }
    }
  }
};
}  // namespace ae

#endif  // AETHER_POLLER_POLLER_TYPES_H_
