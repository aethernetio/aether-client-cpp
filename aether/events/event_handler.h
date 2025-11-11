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

#ifndef AETHER_EVENTS_EVENT_HANDLER_H_
#define AETHER_EVENTS_EVENT_HANDLER_H_

#include <utility>

#include "aether/config.h"
#include "aether/common.h"
#include "aether/types/small_function.h"

namespace ae {

static constexpr std::size_t kFunctionSize = AE_EVENT_HANDLER_MAX_SIZE;
static constexpr std::size_t kFunctionAlign = AE_EVENT_HANDLER_ALIGN;

template <typename TSignature>
class EventHandler;

/**
 * \brief Object to store event handler callback
 */
template <typename... TArgs>
class EventHandler<void(TArgs...)> final {
 public:
  using Fun = SmallFunction<void(TArgs...), kFunctionSize, kFunctionAlign>;

  template <typename TFunc>
  constexpr explicit EventHandler(TFunc&& func)
      : callback_{std::forward<TFunc>(func)} {}

  template <auto Method>
  constexpr explicit EventHandler(MethodPtr<Method> func_ptr)
      : callback_{func_ptr} {}

  constexpr explicit EventHandler(void (*func_ptr)(TArgs...))
      : callback_{func_ptr} {}

  AE_CLASS_COPY_MOVE(EventHandler);

  constexpr void Invoke(TArgs&&... args) const {
    callback_(std::forward<TArgs>(args)...);
  }

 private:
  Fun callback_;
};
}  // namespace ae
#endif  // AETHER_EVENTS_EVENT_HANDLER_H_
