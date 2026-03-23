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

#ifndef AETHER_EXECUTORS_SCHEDULER_ON_TASKS_H_
#define AETHER_EXECUTORS_SCHEDULER_ON_TASKS_H_

#include "third_party/stdexec/include/stdexec/execution.hpp"

#include "aether/ae_context.h"

namespace ae::ex {
namespace scheduler_on_tasks_internal {

template <typename R>
struct Operation {
  void start() noexcept {
    // just run new task and set value to receiver there
    ae_context.scheduler().Task([&]() { stdexec::set_value(std::move(recv)); });
  }

  R recv;
  AeContext ae_context;
};

struct Sender {
  using sender_concept = stdexec::sender_t;

  template <typename Self>
  static consteval auto get_completion_signatures() noexcept {
    return stdexec::completion_signatures<stdexec::set_value_t()>{};
  }

  template <typename R>
  constexpr auto connect(R&& recv) && noexcept {
    return Operation<R>{.recv = std::forward<R>(recv),
                        .ae_context = std::move(ae_context)};
  }

  AeContext ae_context;
};
}  // namespace scheduler_on_tasks_internal

class SchedulerOnTasks {
 public:
  using scheduler_concept = stdexec::scheduler_t;

  constexpr explicit SchedulerOnTasks(AeContext const& context) noexcept
      : ae_context_{context} {}

  constexpr auto schedule() const noexcept {
    return scheduler_on_tasks_internal::Sender{.ae_context = ae_context_};
  }

  bool operator==(SchedulerOnTasks const&) const = default;

 private:
  AeContext ae_context_;
};
}  // namespace ae::ex

#endif  // AETHER_EXECUTORS_SCHEDULER_ON_TASKS_H_
