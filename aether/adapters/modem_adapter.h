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

#ifndef AETHER_ADAPTERS_MODEM_ADAPTER_H_
#define AETHER_ADAPTERS_MODEM_ADAPTER_H_

#include <cstdint>

#include "aether/config.h"

#if AE_SUPPORT_MODEMS
#  include "aether/events/events.h"

#  include "aether/modems/imodem_driver.h"
#  include "aether/adapters/parent_modem.h"
#  include "aether/access_points/access_point.h"

namespace ae {
class ModemAdapter final : public ParentModemAdapter {
  AE_OBJECT(ModemAdapter, ParentModemAdapter, 0)

  ModemAdapter() = default;

 public:
#  ifdef AE_DISTILLATION
  ModemAdapter(ObjProp prop, ObjPtr<Aether> aether, IPoller::ptr poller,
               ModemInit modem_init);
#  endif  // AE_DISTILLATION

  ~ModemAdapter() override;

  AE_OBJECT_REFLECT(AE_MMBRS(access_point_))

  std::vector<AccessPoint::ptr> access_points() override;

  IModemDriver& modem_driver();

 private:
  bool connected_{false};

  std::unique_ptr<IModemDriver> modem_driver_;
  AccessPoint::ptr access_point_;
};

}  // namespace ae
#endif
#endif  // AETHER_ADAPTERS_MODEM_ADAPTER_H_
