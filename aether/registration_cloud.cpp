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
#  include "aether/channel.h"

namespace ae {

#  ifdef AE_DISTILLATION
RegistrationCloud::RegistrationCloud(ObjPtr<Aether> aether, Domain* domain)
    : Cloud{domain}, aether_{std::move(aether)} {}
#  endif

void RegistrationCloud::AddServerSettings(UnifiedAddress address) {
  auto server = domain_->CreateObj<Server>();
  // don't care for registration
  server->server_id = 0;

  // TODO: use all adapters
  auto adapter = aether_.as<Aether>()->adapter_factories[0];

  auto channel = domain_->CreateObj<Channel>(std::move(adapter));
  channel->address = std::move(address);
  server->AddChannel(std::move(channel));

  AddServer(server);
}

}  // namespace ae

#endif  // AE_SUPPORT_REGISTRATION
