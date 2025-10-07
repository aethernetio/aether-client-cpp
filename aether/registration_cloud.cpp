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

#include "aether/registration_cloud.h"

#if AE_SUPPORT_REGISTRATION
#  include <utility>

#  include "aether/server.h"
#  include "aether/aether.h"

namespace ae {

#  ifdef AE_DISTILLATION
RegistrationCloud::RegistrationCloud(ObjPtr<Aether> aether, Domain* domain)
    : Cloud{domain}, aether_{std::move(aether)} {}
#  endif

void RegistrationCloud::AddServerSettings(UnifiedAddress address) {
  // don't care about server id for registration
  auto server =
      domain_->CreateObj<Server>(ServerId{0}, std::vector{std::move(address)});
  server->Register(aether_.as<Aether>()->adapter_registry);

  AddServer(server);
}

}  // namespace ae

#endif  // AE_SUPPORT_REGISTRATION
