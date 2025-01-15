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

#ifndef AETHER_TELE_DECLARATION_H_
#define AETHER_TELE_DECLARATION_H_

#include <cstdint>
#include <string_view>

#include "aether/tele/levels.h"
#include "aether/tele/modules.h"

namespace ae::tele {
struct Tag {
  std::uint32_t crc_;
  Module::underlined_t module_;
};

struct Declaration {
  std::size_t index_;
  Module::underlined_t module_;
  Level::underlined_t level_;
};

struct CompileOption {
  std::size_t index_;
  std::string_view name_;
  std::uint32_t value_;
};
}  // namespace ae::tele
#endif  // AETHER_TELE_DECLARATION_H_ */
