/* Copyright 2026 Aethernet Inc. */

#include "aether/config.h"
#include "aether/tele.h"

#if (defined(CM_ESP32))
#  include <iostream>
#  include <esp_task_wdt.h>
#endif

extern "C" void app_main();
extern int AetherProbeReceiverExample();

int test(void) { return AetherProbeReceiverExample(); }

#if (defined(ESP_PLATFORM))
void app_main(void) {
  esp_task_wdt_config_t config_wdt = {
      .timeout_ms = 60000, .idle_core_mask = 0, .trigger_panic = true};
  auto err = esp_task_wdt_reconfigure(&config_wdt);
  if (err != 0) {
    std::cerr << "Reconfigure WDT is failed!\n";
  }
  test();
}
#endif

#if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
     defined(__FreeBSD__) || defined(_WIN64) || defined(_WIN32))
int main() { return test(); }
#endif
