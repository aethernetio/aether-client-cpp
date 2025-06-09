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

#include "aether_capi.h"

static const char* kWifiSsid = "Visuale";
static const char* kWifiPass = "Ws63$yhJ";
static const char* kMessage = "Message";

void OnMsg(CUid uid, uint8_t const* data, size_t size, void* user_data){ // message handler func
    printf("Message %s", data);
}

int AetherCApiExample();

int AetherCApiExample() {
  /**
   * Construct a main aether application class.
   * It's include a Domain and Aether instances accessible by getter methods.
   * It has Update, WaitUntil, Exit, IsExit, ExitCode methods to integrate it in
   * your update loop.
   * Also it has action context protocol implementation \see Action.
   * To configure its creation \see AetherAppConstructor.
   */
  
  AetherClassHandle* obj = AetherClassCreate();
  AetherClassInit(obj, &OnMsg, kWifiSsid, kWifiPass);
  AetherClassSendMessages(kMessage, sizeof(kMessage));  
  AetherClassDestroy(obj);
    
  return 0;
}
