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

#ifndef TESTS_TEST_OBJECT_SYSTEM_OBJECTS_POOPA_LOOPA_H_
#define TESTS_TEST_OBJECT_SYSTEM_OBJECTS_POOPA_LOOPA_H_

#include <vector>

#include "aether/obj/obj.h"

namespace ae {
class Poopa;
class Loopa;

class Poopa : public Obj {
  AE_OBJECT(Poopa, Obj, 0)

  Poopa() = default;

 public:
  explicit Poopa(Domain* domain) : Obj{domain} {}
  ~Poopa() override { DeleteCount++; }

  void SetLoopa(Obj::ptr loopa) { this->loopa = std::move(loopa); }

  AE_OBJECT_REFLECT(AE_MMBR(loopa))

  static inline int DeleteCount = 0;
  Obj::ptr loopa;
};

class Loopa : public Obj {
  AE_OBJECT(Loopa, Obj, 0)

  Loopa() = default;

 public:
  explicit Loopa(Domain* domain) : Obj{domain} {}
  ~Loopa() override { DeleteCount++; }

  void AddPoopa(Obj::ptr poopa) { poopas.emplace_back(std::move(poopa)); }

  AE_OBJECT_REFLECT(AE_MMBR(poopas))

  static inline int DeleteCount = 0;
  std::vector<Obj::ptr> poopas;
};

}  // namespace ae

#endif  // TESTS_TEST_OBJECT_SYSTEM_OBJECTS_POOPA_LOOPA_H_
