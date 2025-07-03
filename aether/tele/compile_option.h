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

#ifndef AETHER_TELE_COMPILE_OPTION_H_
#define AETHER_TELE_COMPILE_OPTION_H_

#ifndef AETHER_TELE_TELE_H_
#  error "Include tele.h instead"
#endif

#include <cstdint>
#include <string_view>

namespace ae::tele {
struct CompileOption {
  std::uint32_t index;
  std::string_view name;
  std::string_view value;
};

struct CustomOption {
  std::string_view name;
  std::string_view value;
};
}  // namespace ae::tele
#endif  // AETHER_TELE_COMPILE_OPTION_H_ */
