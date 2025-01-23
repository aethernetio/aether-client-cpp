/*
 * Copyright 2024 Aethernet Inc.
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

#include <vector>

#include "aether/aether.h"
#include "aether/common.h"
#include "aether/global_ids.h"
#include "aether/port/tele_init.h"
#include "aether/literal_array.h"
#include "aether/crypto/sign.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/ae_actions/registration/registration.h"

#if (defined(__linux__) || defined(__unix__) || defined(__FreeBSD__) || \
     defined(_WIN64) || defined(_WIN32) || defined(__APPLE__))
#  include "aether/port/file_systems/file_system_header.h"
#  include "aether/port/file_systems/file_system_std.h"
#  include "aether/adapters/ethernet.h"
#  include "aether/adapters/register_wifi.h"
#elif (defined(ESP_PLATFORM))
#  include "aether/port/file_systems/file_system_header.h"
#  include "aether/port/file_systems/file_system_ram.h"
#  include "aether/port/file_systems/file_system_spifs_v1.h"
#  include "aether/port/file_systems/file_system_spifs_v2.h"
#  include "aether/adapters/esp32_wifi.h"
#endif

#include "aether/tele/tele.h"


static constexpr int wait_time = 100;

namespace ae {

static Aether::ptr LoadAether(Domain& domain) {
  Aether::ptr aether;
  aether.SetId(GlobalId::kAether);
  domain.LoadRoot(aether);
  assert(aether);
  return aether;
}

}  // namespace ae

int AetherRegistered();

int AetherRegistered() {
  ae::TeleInit::Init();
  {
    AE_TELE_ENV();
    AE_TELE_INFO("Started");
    ae::Registry::Log();
  }

  auto fs =
#if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
     defined(__FreeBSD__) || defined(_WIN64) || defined(_WIN32))
      ae::FileSystemHeaderFacility{};
#elif (defined(ESP_PLATFORM))
      ae::FileSystemHeaderFacility{};
#endif

  ae::Domain domain{ae::ClockType::now(), fs};
  ae::Aether::ptr aether = ae::LoadAether(domain);
  ae::TeleInit::Init(aether);

  ae::Adapter::ptr adapter{domain.LoadCopy(aether->adapter_factories.front())};

  std::vector<ae::Client::ptr>clients{};

  // Load clients
  for (std::size_t i{0}; i < aether->clients().size(); i++) {
    clients.insert(clients.begin() + i, aether->clients()[i]);
    aether->domain_->LoadRoot(clients[i]);
    assert(clients[i]);
    // Set adapter for all clouds in the client to work though.
    clients[i]->cloud()->set_adapter(adapter);
  }

  while (1) {
    auto now = ae::Now();
    auto next_time = aether->domain_->Update(now);
    aether->action_processor->get_trigger().WaitUntil(
        std::min(next_time, now + std::chrono::milliseconds(wait_time)));
#if ((defined(ESP_PLATFORM)))
    auto heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    AE_TELED_INFO("Heap size {}", heap_free);
#endif
  }

  aether->domain_->Update(ae::ClockType::now());

  // save objects state
  domain.SaveRoot(aether);
}
