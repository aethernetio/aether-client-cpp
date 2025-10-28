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
#include "aether/capi.h"

char const* message = "Major Tom to ground control";

void MessageReceived(AetherClient* client, CUid sender, void const* data,
                     size_t size, void* user_data) {
  printf("Received message size: %zu test: %s\n", size, (char const*)data);

  AetherExit(0);
}

int AetherOddity(char const* destination_uid) {
  // This init aether library
  // Send a message and wait for response

  ClientConfig client_config = {
      .id = 0,
      .parent_uid = CUidFromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"),
      .message_received_cb = MessageReceived,
  };

  AetherConfig aether_config = {
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
