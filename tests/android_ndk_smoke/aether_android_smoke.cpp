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

#include <cstdio>
#include <memory>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/idomain_storage.h"

#include "aether/aether_app.h"
#include "aether/common.h"

namespace {

std::unique_ptr<ae::IDomainStorage> MakeRamDomainStorage() {
  return std::make_unique<ae::RamDomainStorage>();
}

}  // namespace

extern "C" int aether_android_smoke_run() {
  std::fprintf(stdout, "AETHER_ANDROID_SMOKE_START\n");
  std::fflush(stdout);

  {
    auto app =
        ae::AetherApp::Construct(ae::AetherAppContext{MakeRamDomainStorage});
    if (app.get() == nullptr) {
      std::fprintf(stderr, "AETHER_ANDROID_SMOKE_FAIL construct\n");
      std::fflush(stderr);
      return 1;
    }

    std::fprintf(stdout, "AETHER_ANDROID_APP_CONSTRUCTED\n");
    std::fflush(stdout);

    for (int i = 0; i < 5; ++i) {
      (void)app->Update(ae::Now());
    }

    std::fprintf(stdout, "AETHER_ANDROID_UPDATE_OK\n");
    std::fflush(stdout);
  }

  std::fprintf(stdout, "AETHER_ANDROID_SMOKE_OK\n");
  std::fflush(stdout);
  return 0;
}
