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

#include <tuple>
#include <cstdint>

#include "aether/format/format.h"

namespace ae {
enum class EventType : std::uint8_t {
  kRead,
  kWrite,
  kError,
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

  operator Handle() const { return descriptor; }
  operator Socket() const { return reinterpret_cast<Socket>(descriptor); }

  Handle descriptor;
#else
  DescriptorType(int des) : descriptor{des} {}

  operator int() const { return descriptor; }

  int descriptor;
#endif
};

struct PollerEvent {
  DescriptorType descriptor;
  EventType event_type;

  bool operator>(const PollerEvent& evt) const {
    return this->descriptor.descriptor > evt.descriptor.descriptor;
  }

  bool operator<(const PollerEvent& evt) const {
    return this->descriptor.descriptor < evt.descriptor.descriptor;
  }

  bool operator==(const PollerEvent& evt) const {
    return std::tie(this->descriptor.descriptor, this->event_type) ==
           std::tie(evt.descriptor.descriptor, evt.event_type);
  }
};

template <>
struct Formatter<EventType> {
  template <typename TStream>
  void Format(EventType const& value, FormatContext<TStream>& ctx) {
    auto str_value = [](auto value) {
      switch (value) {
        case EventType::kRead:
          return std::string_view{"kRead"};
        case EventType::kWrite:
          return std::string_view{"kWrite"};
        case EventType::kError:
          return std::string_view{"kError"};
        default:
          break;
      }
      return std::string_view{"UNKNOWN"};
    }(value);
    ctx.out().write(str_value);
  }
};

template <>
struct Formatter<PollerEvent> {
  template <typename TStream>
  void Format(PollerEvent const& value, FormatContext<TStream>& ctx) {
    ae::Format(ctx.out(), "Descriptor: {} PollerEvent type: {}",
               value.descriptor.descriptor, value.event_type);
  }
};

}  // namespace ae

#endif  // AETHER_POLLER_POLLER_TYPES_H_
