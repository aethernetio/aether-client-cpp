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

#include "aether/env.h"

#include "aether/aether.h"

namespace ae {
AeEnv::AeEnv() : scheduler_{}, event_system_{} {}

void AeEnv::SetAether(ObjPtr<Aether> const& aether) noexcept {
  Ptr<Aether> p = aether.Load();
  aether_ = p;
}
TaskScheduler& AeEnv::scheduler() const noexcept { return scheduler_; }
EventSystem& AeEnv::event_system() const noexcept { return event_system_; }

void* AeEnv::find_component(EnvId id) noexcept {
  if (id == EnvTypeId<Aether>::value) {
    auto ptr = aether_.Lock();
    assert(!!ptr && "Aether must be set and loaded");
    return ptr.get();
  }
  if (id == EnvTypeId<TaskScheduler>::value) {
    return &scheduler_;
  }
  if (id == EnvTypeId<EventSystem>::value) {
    return &event_system_;
  }

  return nullptr;
}
}  // namespace ae
