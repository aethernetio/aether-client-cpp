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

#include "aether/config.h"

#include "aether/tasks/manual_task_scheduler.h"

namespace ae {
class Aether;
using TaskScheduler = ManualTaskScheduler<
    TaskManagerConf<AE_TASK_MAX_COUNT, AE_TASK_MAX_SIZE, AE_TASK_ALIGN>>;

struct AeCtxTable {
  Aether& (*aether_getter)(void* obj);
  TaskScheduler& (*scheduler_getter)(void* obj);
};

struct AeCtx {
  void* obj;
  AeCtxTable const* vtable;
};

template <typename T>
concept AeContextual = requires(T& t) {
  { t.ToAeContext() } -> std::same_as<AeCtx>;
};

class AeContext {
 public:
  template <AeContextual T>
  constexpr AeContext(T& obj)  // NOLINT(*explicit-constructor)
      : ctx_{obj.ToAeContext()} {}

  Aether& aether() const { return ctx_.vtable->aether_getter(ctx_.obj); }
  TaskScheduler& scheduler() const {
    return ctx_.vtable->scheduler_getter(ctx_.obj);
  }

 private:
  AeCtx ctx_;
};
}  // namespace ae

#endif  // AETHER_AE_CONTEXT_H_
