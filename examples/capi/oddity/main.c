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

#include <stdio.h>

#if (defined(CM_ESP32))
#  include <freertos/FreeRTOS.h>
#  include <esp_log.h>
#  include <esp_task_wdt.h>
#endif

// common main function for ESP and desktops
extern int AetherOddity(char const* destination_uid);

#if (defined(ESP_PLATFORM))
extern "C" void app_main(void) {
  esp_task_wdt_config_t config_wdt = {
      .timeout_ms = 60000,
      .idle_core_mask = 0,  // i.e. do not watch any idle task
      .trigger_panic = true};

  esp_err_t err = esp_task_wdt_reconfigure(&config_wdt);
  if (err != 0) {
    fprintf(stderr, "Reconfigure WDT is failed!\n");
  }

  // esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0));
  // esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(1));

  AetherOddity("");
}
#endif

#if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
     defined(__FreeBSD__) || defined(_WIN64) || defined(_WIN32))

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <destination_uid>\n", argv[0]);
    return 1;
  }

  return AetherOddity(argv[1]);
}
#endif
