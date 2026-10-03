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

#ifndef AETHER_ENV_H_
#define AETHER_ENV_H_

#include "aether-objects/env/env.h"
#include "aether-objects/obj/obj_ptr.h"
#include "aether-objects/ptr/ptr_view.h"

#include "aether/events/events.h"
#include "aether/tasks/manual_task_scheduler.h"

namespace ae {
class Aether;

/**
 * \brief Aether environment object
 * It's possible to extend this type and provide your components through it.
 * Or provide your own type \see aether_app
 */
class AeEnv : public Env {
 public:
  AeEnv();

  void SetAether(ObjPtr<Aether> const& aether) noexcept;

  TaskScheduler& scheduler() const noexcept;
  EventSystem& event_system() const noexcept;

 protected:
  void* find_component(EnvId id) noexcept override;

  mutable TaskScheduler scheduler_;
  mutable EventSystem event_system_;
  PtrView<Aether> aether_;
};

}  // namespace ae

#endif  // AETHER_ENV_H_
