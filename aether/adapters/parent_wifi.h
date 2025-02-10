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

#ifndef AETHER_ADAPTERS_PARENT_WIFI_H_
#define AETHER_ADAPTERS_PARENT_WIFI_H_

#include <string>

#include "aether/adapters/adapter.h"
#include "aether/poller/poller.h"

namespace ae {
class Aether;

class ParentWifiAdapter : public Adapter {
  AE_OBJECT(ParentWifiAdapter, Adapter, 0)

 protected:
  ParentWifiAdapter() = default;

 public:
#ifdef AE_DISTILLATION
  ParentWifiAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
                    std::string ssid, std::string pass, Domain* domain);
#endif  // AE_DISTILLATION

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
    dnv(aether_, poller_);
    dnv(ssid_, pass_);
  }

  Obj::ptr aether_;
  IPoller::ptr poller_;

  std::string ssid_;
  std::string pass_;
};
}  // namespace ae

#endif  // AETHER_ADAPTERS_PARENT_WIFI_H_
