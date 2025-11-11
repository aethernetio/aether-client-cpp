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

extern int test_literal_array();
extern int test_ring_buffer();
extern int test_concat_arrays();
extern int test_span();
extern int test_static_map();
extern int test_statistics_counter();
extern int test_aligned_storage();
extern int test_type_list();
extern int test_uid();
extern int test_small_function();

int main() {
  int res = 0;
  res += test_literal_array();
  res += test_ring_buffer();
  res += test_concat_arrays();
  res += test_span();
  res += test_static_map();
  res += test_statistics_counter();
  res += test_aligned_storage();
  res += test_type_list();
  res += test_uid();
  res += test_small_function();
  return res;
}
