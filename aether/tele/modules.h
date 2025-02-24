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

#ifndef AETHER_TELE_MODULES_H_
#define AETHER_TELE_MODULES_H_

#ifndef AETHER_TELE_TELE_H_
#  error "Include tele.h instead"
#endif

#include <cstdint>
#include <string>
#include <string_view>

namespace ae::tele {
struct Module {
  static std::string text(Module const& value) {
    return std::string{value.name};
  }

  std::uint32_t value;
  std::string_view name;
};
}  // namespace ae::tele

#define AE_TELE_MODULE(NAME, VALUE) \
  inline constexpr auto NAME = ae::tele::Module { VALUE, #NAME, }

#endif  // AETHER_TELE_MODULES_H_
