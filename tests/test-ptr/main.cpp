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

#include <unity.h>

void setUp() {}
void tearDown() {}

extern int test_rc_ptr();
extern int test_rc_ptr_bench();
extern int test_shared_ptr_bench();
extern int run_test_ptr();
extern int test_ptr_cycles();
extern int test_ptr_view();
extern int test_ptr_inheritance();
extern int test_storage();

int main() {
  int res = 0;

  res += test_rc_ptr();
  res += run_test_ptr();
  res += test_ptr_cycles();
  res += test_ptr_view();
  res += test_ptr_inheritance();
  res += test_storage();

#if defined AE_RC_PTR_BENCH
  res += test_rc_ptr_bench();
  res += test_shared_ptr_bench();
#endif
  return res;
}
