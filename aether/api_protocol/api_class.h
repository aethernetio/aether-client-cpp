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

#ifndef AETHER_API_PROTOCOL_API_CLASS_H_
#define AETHER_API_PROTOCOL_API_CLASS_H_

#include "aether/api_protocol/protocol_context.h"

namespace ae {
class ApiClass {
 public:
  explicit ApiClass(ProtocolContext& protocol_context)
      : protocol_context_{&protocol_context} {}

  ProtocolContext& protocol_context() const { return *protocol_context_; }

 private:
  ProtocolContext* protocol_context_;
};
}  // namespace ae

#endif  // AETHER_API_PROTOCOL_API_CLASS_H_
