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

#ifndef AETHER_EXECUTORS_EXECUTORS_H_
#define AETHER_EXECUTORS_EXECUTORS_H_

#include "aether/warning_disable.h"

// IWYU pragma: begin_exports
DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include <stdexec/execution.hpp>

#include <exec/variant_sender.hpp>
DISABLE_WARNING_POP()

#include "aether/executors/for_range.h"
#include "aether/executors/any_sender.h"
#include "aether/executors/any_waiter.h"
#include "aether/executors/make_sender.h"
#include "aether/executors/sync_waiter.h"
#include "aether/executors/async_waiter.h"
#include "aether/executors/with_timeout.h"
#include "aether/executors/scheduler_on_tasks.h"
// IWYU pragma: end_exports

namespace ae::ex {
using namespace STDEXEC;                  // NOLINT(*using-namespace)
using namespace experimental::execution;  // NOLINT(*using-namespace)
}  // namespace ae::ex

#endif  // AETHER_EXECUTORS_EXECUTORS_H_
