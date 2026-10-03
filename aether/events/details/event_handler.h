/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_EVENTS_DETAILS_EVENT_HANDLER_H_
#define AETHER_EVENTS_DETAILS_EVENT_HANDLER_H_

#include <atomic>
#include <cstdint>

namespace ae::events {
/**
 * \brief The basic interface for event handlers to store pointers.
 */
class IHandler {
 public:
  IHandler() = default;
  virtual ~IHandler() = default;

  IHandler(IHandler const& other) = delete;
  IHandler& operator=(IHandler const& other) = delete;

  IHandler(IHandler&& other) noexcept
      : active{other.active.load()}, ref_cntr{other.ref_cntr.load()} {}
  IHandler& operator=(IHandler&& other) noexcept {
    if (this != &other) {
      active = other.active.load();
      ref_cntr = other.ref_cntr.load();
    }
    return *this;
  }

  std::atomic<bool> active{true};
  std::atomic<std::uint16_t> ref_cntr{0};
};

template <typename Signature>
class Handler;

template <typename... Args>
class Handler<void(Args...)> : public IHandler {
 public:
  // Implementations must not throw. Under the project's no-exceptions
  // configuration, a throwing callback terminates.
  virtual void Invoke(Args... args) noexcept = 0;
};
}  // namespace ae::events

#endif  // AETHER_EVENTS_DETAILS_EVENT_HANDLER_H_
