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

#include "c_api.h"

static const char* kWifiSsid = "Test1234";
static const char* kWifiPass = "Test1234";
static const char* kMessage = "Message\r\n";
 
void OnMsg(CUid uid, uint8_t const* data, size_t size,
           void* user_data) {  // message handler func
  for (size_t i = 0; i < size; i++) {
    printf("%c", data[i]);
  }
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
  int ret = 0;

  // Config aether
  struct aether_cfg cfg;
  CUid id;

  for(uint8_t i=0; i<16; i++){
    id.id[i]=0;
  }
    
  cfg.msg_handler=OnMsg;
  cfg.msg_handler_user_data=kMessage;
  cfg.parent_uid=id;
  cfg.wifi_ssid=kWifiSsid;
  cfg.wifi_pass=kWifiPass;

  AetherClassHandle* obj = AetherClassCreate(cfg);
  AetherClassInit(obj);
  AetherClassSendMessages(obj, kMessage, 9);
  ret = AetherClassDestroy(obj);

  return ret;
}
