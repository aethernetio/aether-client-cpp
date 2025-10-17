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

#include "aether/tele/tele_init.h"

void setUp() { ae::tele::TeleInit::Init(); }
void tearDown() {}

extern int test_at_support();
extern int test_at_buffer();
extern int test_at_dispatcher();
extern int test_at_listener();
extern int test_at_request();

int main() {
  int res = 0;
  res += test_at_support();
  res += test_at_buffer();
  res += test_at_dispatcher();
  res += test_at_listener();
  res += test_at_request();
  return res;
}
