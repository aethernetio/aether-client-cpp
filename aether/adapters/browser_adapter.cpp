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

#include "aether/adapters/browser_adapter.h"

#include <utility>

#include "aether/aether.h"
#include "aether/adapters/adapter_tele.h"

namespace ae {

#ifdef AE_DISTILLATION
BrowserAdapter::BrowserAdapter(ObjProp prop, ObjPtr<Aether> aether)
    : Adapter{prop},
      browser_access_point_{
          BrowserAccessPoint::ptr::Create(domain, std::move(aether))} {
  AE_TELED_INFO("BrowserAdapter created");
}
#endif  // AE_DISTILLATION

std::vector<AccessPoint::ptr> BrowserAdapter::access_points() {
  return {browser_access_point_};
}

}  // namespace ae
