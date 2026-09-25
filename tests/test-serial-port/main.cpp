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
#include <string_view>

#include "aether/tele.h"

void setUp() {
  TELE_SINK::Instance().SetTrap(
      std::make_shared<ae::tele::IoStreamTrap>(std::cout));
}
void tearDown() {}

extern int test_at_support();
extern int test_at_buffer();
extern int test_at_dispatcher();
extern int test_at_listener();
extern int test_at_request();
extern int test_at_stages();
extern int test_thingy91x();
extern int test_sim7070();
extern int test_app_shutdown();
extern int test_modem_transport_shutdown();
extern int test_win_serial_reopen(char const* port);

int main(int argc, char* argv[]) {
  if (argc == 3 && std::string_view{argv[1]} == "--serial-reopen") {
    return test_win_serial_reopen(argv[2]);
  }
  if (argc == 2 && std::string_view{argv[1]} == "--modem-shutdown") {
    return test_thingy91x() + test_sim7070() + test_app_shutdown() +
           test_modem_transport_shutdown();
  }
  int res = 0;
  res += test_at_support();
  res += test_at_buffer();
  res += test_at_dispatcher();
  res += test_at_listener();
  res += test_at_request();
  res += test_at_stages();
  res += test_thingy91x();
  res += test_sim7070();
  res += test_app_shutdown();
  res += test_modem_transport_shutdown();
  return res;
}
