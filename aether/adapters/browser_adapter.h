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

#ifndef AETHER_ADAPTERS_BROWSER_ADAPTER_H_
#define AETHER_ADAPTERS_BROWSER_ADAPTER_H_

#include "aether-objects/obj/dummy_obj.h"  // IWYU pragma: keep
#include "aether/adapters/adapter.h"

#include "aether/access_points/browser_access_point.h"

namespace ae {
class Aether;

/**
 * \brief Browser (Emscripten) network adapter.
 * Creates channels for HTTP/HTTPS/WS/WSS endpoints advertised as BrowserAddr
 * (or NamedAddr milestone overrides).
 */
class BrowserAdapter final : public Adapter {
  AE_OBJECT(BrowserAdapter, Adapter, 0)

  BrowserAdapter() = default;

 public:
#ifdef AE_DISTILLATION
  BrowserAdapter(ObjProp prop, ObjPtr<Aether> aether);
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT(AE_MMBRS(browser_access_point_))

  std::vector<AccessPoint::ptr> access_points() override;

 private:
  BrowserAccessPoint::ptr browser_access_point_;
};
}  // namespace ae

#endif  // AETHER_ADAPTERS_BROWSER_ADAPTER_H_
