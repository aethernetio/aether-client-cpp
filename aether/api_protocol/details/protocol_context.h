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

#ifndef AETHER_API_PROTOCOL_DETAILS_PROTOCOL_CONTEXT_H_
#define AETHER_API_PROTOCOL_DETAILS_PROTOCOL_CONTEXT_H_

#include <cassert>
#include <cstdint>

#include <etl/generic_pool.h>

#include "aether/warning_disable.h"
DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include <etl/vector.h>
DISABLE_WARNING_POP()

#include "aether/config.h"

#include "aether/actions/action_context.h"
#include "aether/events/events.h"
#include "aether/tasks/manual_task_scheduler.h"

#include "aether/api_protocol/details/api_promise.h"
#include "aether/api_protocol/details/request_id.h"

namespace ae {
class PacketReader;

class ProtocolContext {
 public:

  explicit ProtocolContext(ActionContext auto const& context)
      : ProtocolContext{
            context.scheduler(),
            context.event_system(),
        } {}
  ProtocolContext(TaskScheduler& scheduler, EventSystem& event_system);
  ~ProtocolContext();

  TaskScheduler& scheduler() const;
  EventSystem& event_system() const;

 private:
  TaskScheduler* scheduler_;
  EventSystem* event_system_;
};

};
}  // namespace ae

#endif  // AETHER_API_PROTOCOL_DETAILS_PROTOCOL_CONTEXT_H_