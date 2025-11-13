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

#include "aether/obj/obj_id.h"

#include <cstdlib>

namespace ae {
ObjId ObjId::GenerateUnique() {
  // set seed once
  static bool const seed =
      (std::srand(static_cast<unsigned int>(time(nullptr))), true);
  (void)seed;
  // new id would be bigger than any user defined id
  auto value = std::rand() + 10000;
  return ObjId{static_cast<ObjId::Type>(value)};
}
}  // namespace ae
