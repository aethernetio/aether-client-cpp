/*
 * Copyright 2026 Aethernet Inc.
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

extern void test_package_ptr_start_success();
extern void test_package_ptr_start_retry_then_success();
extern void test_package_ptr_start_fail();
extern void test_package_ptr_stop();

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_package_ptr_start_success);
  RUN_TEST(test_package_ptr_start_retry_then_success);
  RUN_TEST(test_package_ptr_start_fail);
  RUN_TEST(test_package_ptr_stop);
  return UNITY_END();
}
