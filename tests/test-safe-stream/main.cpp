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

#include <iostream>
#include "aether/tele.h"

void setUp() {
  TELE_SINK::Instance().SetTrap(
      std::make_shared<ae::tele::IoStreamTrap>(std::cout));
}
void tearDown() {}

extern int test_circular_buffer();
extern int test_sending_chunk_list();
extern int test_receiving_chunks();
extern int test_safe_stream_send();
extern int test_safe_stream_recv();
extern int test_safe_stream_send_recv();
extern int test_safe_stream();
extern int test_safe_stream_reliability();

int main() {
  int res = 0;
  res += test_circular_buffer();
  res += test_sending_chunk_list();
  res += test_receiving_chunks();
  res += test_safe_stream_send();
  res += test_safe_stream_recv();
  res += test_safe_stream_send_recv();
  res += test_safe_stream();
  res += test_safe_stream_reliability();
  return res;
}
