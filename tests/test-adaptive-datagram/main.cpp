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
int test_rate_controller();
int test_emulated_socket();
int test_adaptive_stream();
int test_recovery_controller();
int test_saturation();
int test_p2p_stream_write_budget();

int main() {
  return test_rate_controller() + test_emulated_socket() +
         test_adaptive_stream() + test_recovery_controller() +
         test_saturation() + test_p2p_stream_write_budget();
}
