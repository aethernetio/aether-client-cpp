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

#ifndef AETHER_SERIAL_PORTS_AT_OPERATION_H_
#define AETHER_SERIAL_PORTS_AT_OPERATION_H_

#include "aether/ae_context.h"
#include "aether/actions/action.h"
#include "aether/executors/executors.h"
#include "aether/actions/actions_queue.h"

namespace ae {
template <typename S>
class SenderOperation final : public Action {
 public:
  explicit SenderOperation(AeContext const& ae_context, S&& s) {}

 private:
};
}  // namespace ae

#endif  // AETHER_SERIAL_PORTS_AT_OPERATION_H_
