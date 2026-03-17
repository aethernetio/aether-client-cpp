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

extern int AetherGC();

#ifdef ESP_PLATFORM
void app_main() {
  esp_task_wdt_config_t config_wdt = {
      .timeout_ms = 60000,
      .idle_core_mask = 0,  // i.e. do not watch any idle task
      .trigger_panic = true};

  esp_err_t err = esp_task_wdt_reconfigure(&config_wdt);
  if (err != 0) {
    fprintf(stderr, "Reconfigure WDT is failed!\n");
  }

  AetherGC();
}
#endif

#if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
     defined(__FreeBSD__) || defined(_WIN64) || defined(_WIN32))
int main(int argc, char* argv[]) { return AetherGC(); }
#endif

#include "aether/capi.h"

void PrintUid(CUid const* uid) {
  int i = 0;
  for (; i < 4; i++) {
    printf("%02x", uid->value[i]);
  }
  printf("-");
  for (; i < 6; i++) {
    printf("%02x", uid->value[i]);
  }
  printf("-");
  for (; i < 8; i++) {
    printf("%02x", uid->value[i]);
  }
  printf("-");
  for (; i < 10; i++) {
    printf("%02x", uid->value[i]);
  }
  printf("-");
  for (; i < 16; i++) {
    printf("%02x", uid->value[i]);
  }
}

#define TEST_UID "3ac93165-3d37-4970-87a6-fa4ee27744e4"
char const* message = "Ground control to Major Tom";

void ClientSelected(AetherClient* client, void* user_data) {
  CUid uid;
  GetClientUid(client, &uid);
  printf(">>> Loaded clieint with uid:");
  PrintUid(&uid);
  printf("<<<\n");
}

void MessageSentCb(ActionStatus status, void* user_data) { AetherExit(0); }

void MessageReceived(AetherClient* client, CUid sender, void const* data,
                     size_t size, void* user_data) {
  printf(">>> Received message size: %zu test: %s\n", size, (char const*)data);
  // send message and wait till it sent
  SendStr(sender, message, MessageSentCb, NULL);
}

int AetherGC() {
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
      .id = "ground control",
      .parent_uid = CUidFromString(TEST_UID),
      .client_selected_cb = ClientSelected,
      .message_received_cb = MessageReceived,
  };

  AetherConfig aether_config = {
#ifdef ESP_PLATFORM
      .adapters = &adapters,
#endif
      .default_client = &client_config,
  };

  AetherInit(&aether_config);

  while (AetherExcited() == AE_NOK) {
    uint64_t time = AetherUpdate();
    AetherWait(time);
  }
  return AetherEnd();
}
