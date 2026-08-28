/*
 * Copyright 2025 Aethernet Inc.
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

extern int test_method_call();
extern int test_uap_receive_schedule();
extern int test_uap_peer_deadline_classify();
extern int test_uap_peer_timing();
extern int test_client_online_timing();

int main() {
  int res = 0;
  res += test_method_call();
  res += test_uap_receive_schedule();
  res += test_uap_peer_deadline_classify();
  res += test_uap_peer_timing();
  res += test_client_online_timing();

  return res;
}
