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

extern int test_safe_stream_types();
extern int test_sending_chunk_list();
extern int test_send_data_buffer();
extern int test_receiving_chunks();
extern int test_safe_stream_send();
extern int test_safe_stream_recv();
extern int test_safe_stream_send_recv();
extern int test_safe_stream();
extern int test_safe_stream_reliability();
extern int test_crypto_stream();
extern int test_protocol_stream();
extern int test_templated_streams();
extern int test_tied_gates();

int main() {
  int res = 0;
  res += test_safe_stream_types();
  res += test_sending_chunk_list();
  res += test_send_data_buffer();
  res += test_receiving_chunks();
  res += test_safe_stream_send();
  res += test_safe_stream_recv();
  res += test_safe_stream_send_recv();
  res += test_safe_stream();
  res += test_safe_stream_reliability();
  res += test_crypto_stream();
  res += test_protocol_stream();
  res += test_templated_streams();
  res += test_tied_gates();
  return res;
}
