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

#ifndef AETHER_WORK_CLOUD_API_TELEMETRIC_H_
#define AETHER_WORK_CLOUD_API_TELEMETRIC_H_

#include <vector>
#include <string>
#include <cstdint>

#include "aether/reflect/reflect.h"

namespace ae {
struct Telemetric {
  struct Cpp {
    AE_REFLECT_MEMBERS(utm_id, blob, lib_version, os, compiler)

    std::uint32_t utm_id;
    std::vector<std::uint8_t> blob;

    std::string lib_version;
    std::string os;
    std::string compiler;
  };

  AE_REFLECT_MEMBERS(type, cpp);
  std::uint8_t const type = 0;  //< Cpp type
  Cpp cpp;
};
}  // namespace ae

#endif  // AETHER_WORK_CLOUD_API_TELEMETRIC_H_
