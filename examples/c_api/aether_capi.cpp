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
#include "examples/c_api/aether_class.h"
#include "aether/aether_app.h"  // Our C++ class
#include "aether/ae_actions/registration/registration.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"

#include "aether/adapters/ethernet.h"
#include "aether/adapters/esp32_wifi.h"

#include "aether/port/tele_init.h"
#include "aether/tele/tele.h"

extern "C" {

struct AetherClassHandle {
  ae::Ptr<ae::AetherApp> aether_app;  // Real C++ object
};

std::shared_ptr<ae::c_api_test::CApiTestAction> capi_test_action;

AetherClassHandle* AetherClassCreate(struct aether_cfg cfg){
  AetherClassHandle* handle = new AetherClassHandle;
  
    /**
   * Construct a main aether application class.
   * It's include a Domain and Aether instances accessible by getter methods.
   * It has Update, WaitUntil, Exit, IsExit, ExitCode methods to integrate it in
   * your update loop.
   * Also it has action context protocol implementation \see Action.
   * To configure its creation \see AetherAppConstructor.
   */
  handle->aether_app = ae::AetherApp::Construct(
      ae::AetherAppConstructor{}
#if defined AE_DISTILLATION
          .Adapter([cfg](
                       ae::Domain* domain,
                       ae::Aether::ptr const& aether)->ae::Adapter::ptr {
#  if defined ESP32_WIFI_ADAPTER_ENABLED
            auto adapter = domain->CreateObj<ae::Esp32WifiAdapter>(
                ae::GlobalId::kEsp32WiFiAdapter, aether, aether->poller,
                std::string(cfg.wifi_ssid), std::string(cfg.wifi_pass));
#  else
            auto adapter = domain->CreateObj<ae::EthernetAdapter>(
                ae::GlobalId::kEthernetAdapter, aether, aether->poller);
#  endif
            return adapter;
          })
#endif
  );
  
  capi_test_action = std::make_shared<ae::c_api_test::CApiTestAction>(handle->aether_app, cfg.msg_handler);
  
  capi_test_action->ResultEvent().Subscribe(
      [&](auto const&) { handle->aether_app->Exit(0); });
  capi_test_action->ErrorEvent().Subscribe(
      [&](auto const&) { handle->aether_app->Exit(1); });
      
  return handle;
}

int AetherClassDestroy(AetherClassHandle* handle) {
  int res = handle->aether_app->ExitCode();
  if (handle) {
    delete handle;
  }
  return res;
}

void AetherClassInit(AetherClassHandle* handle) {

  while (capi_test_action->GetState() != ae::c_api_test::State::kSendMessages) {
    auto current_time = ae::Now();
    auto next_time = handle->aether_app->Update(current_time);
    handle->aether_app->WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
  }  
}

void AetherClassSendMessages(AetherClassHandle* handle, char const* data, size_t size) {
  auto current_time = ae::Now();
  std::string msg{data, data + size};
  capi_test_action->SendMessages(current_time, msg);
  
  while (!handle->aether_app->IsExited()) {
    auto current_time = ae::Now();
    auto next_time = handle->aether_app->Update(current_time);
    handle->aether_app->WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
  }  
}

}  // extern "C"
