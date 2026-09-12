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

extern int test_api_protocol_method_call();
extern int test_api_protocol_packet();
extern int test_api_protocol_pending_responses();
extern int test_api_protocol_registration();
extern int test_api_protocol_request_response();
extern int test_api_protocol_sender_lifecycle();

int main() {
  int res = 0;
  res += test_api_protocol_packet();
  res += test_api_protocol_method_call();
  res += test_api_protocol_request_response();
  res += test_api_protocol_pending_responses();
  res += test_api_protocol_sender_lifecycle();
  res += test_api_protocol_registration();

  return res;
}
