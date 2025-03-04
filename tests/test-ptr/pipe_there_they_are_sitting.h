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

#ifndef TESTS_TEST_PTR_PIPE_THERE_THEY_ARE_SITTING_H_
#define TESTS_TEST_PTR_PIPE_THERE_THEY_ARE_SITTING_H_

#include "aether/reflect/reflect.h"

namespace ae::test_ptr {
struct A {
  A() = default;
  virtual ~A() { ++a_destroyed; }

  static inline int a_destroyed = 0;
  int x;
  AE_CLASS_REFLECT(x)
};

struct B : public A {
  B() : A() {}
  ~B() override { ++b_destroyed; }

  static inline int b_destroyed = 0;
  int y;
  AE_CLASS_REFLECT(y)
};
}  // namespace ae::test_ptr
#endif  // TESTS_TEST_PTR_PIPE_THERE_THEY_ARE_SITTING_H_
