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

#if (defined(CM_ESP32))
#  include <esp_task_wdt.h>
#  include <iostream>
#endif

extern "C" void app_main();
extern int MessageServerExample();

int run(void) { return MessageServerExample(); }

#if (defined(ESP_PLATFORM))
void app_main(void) {
  esp_task_wdt_config_t config_wdt = {
      .timeout_ms = 60000, .idle_core_mask = 0, .trigger_panic = true};
  auto err = esp_task_wdt_reconfigure(&config_wdt);
  if (err != 0) {
    std::cerr << "Reconfigure WDT is failed!\n";
  }
  run();
}
#endif

#if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
     defined(__FreeBSD__) || defined(_WIN64) || defined(_WIN32))
int main() { return run(); }
#endif
