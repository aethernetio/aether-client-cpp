/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_WRITE_ACTION_FAILED_WRITE_ACTION_H_
#define AETHER_WRITE_ACTION_FAILED_WRITE_ACTION_H_

#include "aether/write_action/write_action.h"

namespace ae {
/**
 * \brief Immediate failed write action.
 */
class FailedWriteAction final : public WriteAction {
 public:
  explicit FailedWriteAction(ActionContext action_context);
};
}  // namespace ae

#endif  // AETHER_WRITE_ACTION_FAILED_WRITE_ACTION_H_
