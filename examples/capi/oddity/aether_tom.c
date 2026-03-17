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

#include <stdio.h>

#ifdef ESP_PLATFORM
#  include <freertos/FreeRTOS.h>
#  include <esp_log.h>
#  include <esp_task_wdt.h>
#endif

extern int AetherTom(char const* destination_uid);

#ifdef ESP_PLATFORM
void app_main(void) {
  esp_task_wdt_config_t config_wdt = {
      .timeout_ms = 60000,
      .idle_core_mask = 0,  // i.e. do not watch any idle task
      .trigger_panic = true};

  esp_err_t err = esp_task_wdt_reconfigure(&config_wdt);
  if (err != 0) {
    fprintf(stderr, "Reconfigure WDT is failed!\n");
  }

#  ifndef DEST_UID
#    error "DEST_UID should be defined for aether_tom"
#  endif

  AetherTom(DEST_UID);
}
#endif

#if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
     defined(__FreeBSD__) || defined(_WIN64) || defined(_WIN32))
int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <destination_uid>\n", argv[0]);
    return 1;
  }

  return AetherTom(argv[1]);
}
#endif

#include "aether/capi.h"

#define TEST_UID "3ac93165-3d37-4970-87a6-fa4ee27744e4"
char const* message = "Major Tom to ground control";

void MessageReceived(AetherClient* client, CUid sender, void const* data,
                     size_t size, void* user_data) {
  printf(">>> Received message size: %zu test: %s\n", size, (char const*)data);
  AetherExit(0);
}

int AetherTom(char const* destination_uid) {
  // This init aether library
  // Send a message and wait for response

#ifdef ESP_PLATFORM
  AeWifiAdapterConf wifi_adapter_conf = {
      .type = AeWifiAdapter,
      .ssid = WIFI_SSID,
      .password = WIFI_PASSWORD,
  };
  AdapterBase* adapter_confs = (AdapterBase*)&wifi_adapter_conf;
  Adapters adapters = {
      .count = 1,
      .adapters = &adapter_confs,
  };
#endif

  ClientConfig client_config = {
      .id = "Tom",
      .parent_uid = CUidFromString(TEST_UID),
      .message_received_cb = MessageReceived,
  };

  AetherConfig aether_config = {
#ifdef ESP_PLATFORM
      .adapters = &adapters,
#endif
      .default_client = &client_config,
  };

  AetherInit(&aether_config);
  SendStr(CUidFromString(destination_uid), message, NULL, NULL);

  while (AetherExcited() == AE_NOK) {
    uint64_t time = AetherUpdate();
    AetherWait(time);
  }
  return AetherEnd();
}
