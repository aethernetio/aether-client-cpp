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

extern int run_test_domain_tree();
extern int run_test_object_create();
extern int run_test_update_objects();
extern int run_test_version_iterator();

int main() {
  int res{};
  res += run_test_domain_tree();
  res += run_test_object_create();
  res += run_test_version_iterator();
  res += run_test_update_objects();
  return res;
}
