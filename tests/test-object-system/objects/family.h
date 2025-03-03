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

#ifndef TESTS_TEST_OBJECT_SYSTEM_OBJECTS_FAMILY_H_
#define TESTS_TEST_OBJECT_SYSTEM_OBJECTS_FAMILY_H_

#include "aether/obj/obj.h"

namespace ae {
class Father : public Obj {
  AE_OBJECT(Father, Obj, 0)

 protected:
  Father() = default;

 public:
  explicit Father(Domain* domain) : Obj{domain} {}

  AE_OBJECT_REFLECT()
};

class Child : public Father {
  AE_OBJECT(Child, Father, 0)

  Child() = default;

 public:
  explicit Child(Domain* domain) : Father(domain) {}

  AE_OBJECT_REFLECT()
};
}  // namespace ae

#endif  // TESTS_TEST_OBJECT_SYSTEM_OBJECTS_FAMILY_H_
