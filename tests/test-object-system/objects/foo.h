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

#ifndef TESTS_TEST_OBJECT_SYSTEM_OBJECTS_FOO_H_
#define TESTS_TEST_OBJECT_SYSTEM_OBJECTS_FOO_H_

#include "aether/obj/obj.h"

#include "objects/bar.h"

namespace ae {
class Foo : public Obj {
  AE_OBJECT(Foo, Obj, 0)

  Foo() = default;

 public:
  explicit Foo(Domain* domain) : Obj{domain} { bar = domain->CreateObj<Bar>(); }

  AE_OBJECT_REFLECT(a, b, bar)

  int a{1};
  int b{2};
  Bar::ptr bar{};
};
}  // namespace ae

#endif  // TESTS_TEST_OBJECT_SYSTEM_OBJECTS_FOO_H_
