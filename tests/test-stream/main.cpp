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

extern int test_safe_stream_types();
extern int test_safe_stream_sending();
extern int test_safe_stream_receiving();
extern int test_safe_stream();
extern int test_stream_api();
extern int test_crypto_stream();
extern int test_protocol_stream();
extern int test_templated_streams();

int main() {
  int res = 0;
  res += test_safe_stream_types();
  res += test_safe_stream_sending();
  res += test_safe_stream_receiving();
  res += test_safe_stream();
  res += test_stream_api();
  res += test_crypto_stream();
  res += test_protocol_stream();
  res += test_templated_streams();
  return res;
}
