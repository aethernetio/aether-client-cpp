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

#include "aether/config.h"

void setUp() {}
void tearDown() {}

extern int run_test_server_connection_recovery();
extern int run_test_cloud_quarantine_loop();
extern int run_test_cloud_persistence();
#if AE_SUPPORT_REGISTRATION
extern int run_test_registration_root_server_select();
#endif  // AE_SUPPORT_REGISTRATION

int main() {
  int res = 0;
  res += run_test_server_connection_recovery();
  res += run_test_cloud_quarantine_loop();
  res += run_test_cloud_persistence();
#if AE_SUPPORT_REGISTRATION
  res += run_test_registration_root_server_select();
#endif  // AE_SUPPORT_REGISTRATION
  return res;
}
