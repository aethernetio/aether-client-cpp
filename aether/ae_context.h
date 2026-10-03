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

#ifndef AETHER_AE_CONTEXT_H_
#define AETHER_AE_CONTEXT_H_

#include <concepts>

#include "aether-objects/env/env.h"
#include "aether-objects/obj/domain.h"

#include "aether/env.h"

namespace ae {
template <typename T>
concept AeObject = requires(T const& t) {
  requires std::same_as<std::decay_t<decltype(t.domain)>, Domain*>;
};

struct AeCtxTable {
  Aether& (*aether_getter)(void const* obj);
  TaskScheduler& (*scheduler_getter)(void const* obj);
  EventSystem& (*event_system_getter)(void const* obj);
};

template <typename T>
struct AeContextMaker;

template <std::derived_from<Env> T>
struct AeContextMaker<T> {
  static constexpr auto value = AeCtxTable{
      .aether_getter = [](void const* obj) -> decltype(auto) {
        return static_cast<T*>(const_cast<void*>(obj))  // NOLINT(*const-cast*)
            ->template get<Aether>();
      },
      .scheduler_getter = [](void const* obj) -> decltype(auto) {
        return static_cast<T*>(const_cast<void*>(obj))  // NOLINT(*const-cast*)
            ->template get<TaskScheduler>();
      },
      .event_system_getter = [](void const* obj) -> decltype(auto) {
        return static_cast<T*>(const_cast<void*>(obj))  // NOLINT(*const-cast*)
            ->template get<EventSystem>();
      },
  };
};

template <EnvProvider T>
struct AeContextMaker<T> {
  static constexpr auto value = AeCtxTable{
      .aether_getter = [](void const* obj) -> decltype(auto) {
        return get_env<Aether>(*static_cast<T const*>(obj));
      },
      .scheduler_getter = [](void const* obj) -> decltype(auto) {
        return get_env<TaskScheduler>(*static_cast<T const*>(obj));
      },
      .event_system_getter = [](void const* obj) -> decltype(auto) {
        return get_env<EventSystem>(*static_cast<T const*>(obj));
      },
  };
};

template <AeObject T>
struct AeContextMaker<T> {
  static constexpr auto value = AeCtxTable{
      .aether_getter = [](void const* obj) -> decltype(auto) {
        return get_env<Aether>(*static_cast<T const*>(obj)->domain);
      },
      .scheduler_getter = [](void const* obj) -> decltype(auto) {
        return get_env<TaskScheduler>(*static_cast<T const*>(obj)->domain);
      },
      .event_system_getter = [](void const* obj) -> decltype(auto) {
        return get_env<EventSystem>(*static_cast<T const*>(obj)->domain);
      },
  };
};

template <typename T>
concept AeContextual =
    std::same_as<decltype(&AeContextMaker<T>::value), AeCtxTable const*>;

class AeContext {
 public:
  template <AeContextual T>
  constexpr AeContext(T const& obj)  // NOLINT(*explicit-constructor)
      : obj_{&obj}, vtable_{&AeContextMaker<T>::value} {}

  Aether& aether() const { return vtable_->aether_getter(obj_); }
  TaskScheduler& scheduler() const { return vtable_->scheduler_getter(obj_); }
  EventSystem& event_system() const {
    return vtable_->event_system_getter(obj_);
  }

  bool operator==(AeContext const&) const = default;

 private:
  void const* obj_;
  AeCtxTable const* vtable_;
};

}  // namespace ae

#endif  // AETHER_AE_CONTEXT_H_
