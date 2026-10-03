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

#ifndef AETHER_EVENTS_DETAILS_GENERIC_EVENT_HANDLER_H_
#define AETHER_EVENTS_DETAILS_GENERIC_EVENT_HANDLER_H_

#include <concepts>
#include <functional>
#include <utility>

#include "aether/events/details/event_handler.h"

namespace ae::events {
template <typename Signature, typename Func>
class GenericHandler;

template <typename... Args, std::invocable<Args...> Func>
class GenericHandler<void(Args...), Func> final
    : public Handler<void(Args...)> {
 public:
  template <typename U>
    requires(std::same_as<std::decay_t<Func>, std::decay_t<U>>)
  explicit GenericHandler(U&& f) : func_{std::forward<U>(f)} {}

  GenericHandler(GenericHandler const&) = delete;
  GenericHandler& operator=(GenericHandler const&) = delete;

  GenericHandler(GenericHandler&& other) noexcept
      : func_{std::forward<Func>(other.func_)} {}
  GenericHandler& operator=(GenericHandler&&) noexcept = default;
  ~GenericHandler() override = default;

  // Handlers are required to be noexcept for the project's no-exceptions
  // target.
  void Invoke(Args... args) noexcept override {
    std::invoke(func_, std::forward<Args>(args)...);
  }

 private:
  Func func_;
};

}  // namespace ae::events

#endif  // AETHER_EVENTS_DETAILS_GENERIC_EVENT_HANDLER_H_
