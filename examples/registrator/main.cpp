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

#include <string>

#include "aether/config.h"

extern "C" void app_main();
extern int AetherRegistrator(const std::string &ini_file);

// Test function.
int test(const std::string &ini_file) { return AetherRegistrator(ini_file); }

#if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
     defined(__FreeBSD__) || defined(_WIN64) || defined(_WIN32))
int main(int argc, char *argv[]) {
  const std::string ini_file{
      "G:/projects/prj_aether/GitHub/aether-client-cpp/config/config.ini"};

  return test(ini_file);
}
#endif
