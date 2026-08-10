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

#ifndef EXAMPLES_COMMON_AETHER_CONSTRUCT_ETHERNET_H_
#define EXAMPLES_COMMON_AETHER_CONSTRUCT_ETHERNET_H_

#include "aether_construct.h"

#if AE_EXAMPLE_ETHERNET
namespace ae::examples {
static std::unique_ptr<AetherApp> construct_aether_app() {
  return AetherApp::Construct(
      AetherAppContext{}
#  if AE_DISTILLATION
          .AddAdapterFactory([](AetherAppContext const& context) {
            return EthernetAdapter::ptr::Create(
                CreateWith{context.domain()}.with_id(
                    GlobalId::kEthernetAdapter),
                context.aether(), context.poller(), context.dns_resolver());
          })
#  endif
  );
}
}  // namespace ae::examples
#endif

#endif  // EXAMPLES_COMMON_AETHER_CONSTRUCT_ETHERNET_H_
