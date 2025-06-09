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

#include "examples/c_api/aether_capi.h"
#include "examples/c_api/aether_functions.h"
#include "aether/aether_app.h"  // Our C++ class
#include "aether/ae_actions/registration/registration.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"

#include "aether/adapters/ethernet.h"
#include "aether/adapters/esp32_wifi.h"

#include "aether/port/tele_init.h"
#include "aether/tele/tele.h"

extern "C" {

ae::Ptr<ae::AetherApp> aether_app;

struct AetherClassHandle {
  ae::AetherApp* ptr;  // Real C++ object
};

AetherClassHandle* AetherClassCreate() {
  AetherClassHandle* handle = new AetherClassHandle;
  handle->ptr = new ae::AetherApp();  // Calling the C++ constructor
  return handle;
}

void AetherClassDestroy(AetherClassHandle* handle) {
  if (handle) {
    delete handle->ptr;  // Destructor C++
    delete handle;
  }
}

void AetherClassInit(AetherClassHandle* handle, void (*pt2Func)(struct CUid uid, uint8_t const* data,
                                       size_t size, void* user_data), const char* kWifiSsid,
                     const char* kWifiPass) {
  /**
   * Construct a main aether application class.
   * It's include a Domain and Aether instances accessible by getter methods.
   * It has Update, WaitUntil, Exit, IsExit, ExitCode methods to integrate it in
   * your update loop.
   * Also it has action context protocol implementation \see Action.
   * To configure its creation \see AetherAppConstructor.
   */
  State state;
  
  aether_app = handle->ptr->Construct(
      ae::AetherAppConstructor{}
#if defined AE_DISTILLATION
          .Adapter([kWifiSsid, kWifiPass](
                       ae::Domain* domain,
                       ae::Aether::ptr const& aether) -> ae::Adapter::ptr {
#  if defined ESP32_WIFI_ADAPTER_ENABLED
            auto adapter = domain->CreateObj<ae::Esp32WifiAdapter>(
                ae::GlobalId::kEsp32WiFiAdapter, aether, aether->poller,
                std::string(kWifiSsid), std::string(kWifiPass));
#  else
            auto adapter = domain->CreateObj<ae::EthernetAdapter>(
                ae::GlobalId::kEthernetAdapter, aether, aether->poller);
#  endif
            return adapter;
          })
#endif
  );

  SetAether(aether_app);
  
  state = RegisterClients();
  if(state!=State::kConfigureReceiver) return;
  
  auto current_time = ae::Now();
  auto next_time = aether_app->Update(current_time);
  aether_app->WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
        
  ConfigureReceiver(pt2Func);
  if(state!=State::kConfigureSender) return;
  
  current_time = ae::Now();
  next_time = aether_app->Update(current_time);
  aether_app->WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
        
  ConfigureSender();
  if(state!=State::kSendMessages) return;
  
  current_time = ae::Now();
  next_time = aether_app->Update(current_time);
  aether_app->WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
}

void AetherClassSendMessages(char const* data, size_t size){
  auto current_time = ae::Now();
  std::string msg{data, data+size};
  SendMessages(current_time, msg);
}

}  // extern "C"
