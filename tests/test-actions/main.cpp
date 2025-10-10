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

extern int test_action_registry();
extern int test_action_processor();
extern int test_action_status_event();
extern int test_action();
extern int test_pipeline();
extern int test_pipeline_queue();

int main() {
  auto res = 0;
  res += test_action_registry();
  res += test_action_processor();
  res += test_action_status_event();
  res += test_action();
  res += test_pipeline();
  res += test_pipeline_queue();
  return res;
}
