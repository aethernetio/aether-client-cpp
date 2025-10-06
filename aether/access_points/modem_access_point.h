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

#ifndef AETHER_ACCESS_POINTS_MODEM_ACCESS_POINT_H_
#define AETHER_ACCESS_POINTS_MODEM_ACCESS_POINT_H_

#include "aether/actions/action.h"
#include "aether/actions/action_ptr.h"
#include "aether/modems/imodem_driver.h"
#include "aether/adapters/modem_adapter.h"
#include "aether/access_points/access_point.h"

namespace ae {
class Aether;

class ModemConnectAction final : public Action<ModemConnectAction> {
 public:
  ModemConnectAction(ActionContext action_context, IModemDriver& driver);

  UpdateStatus Update();
};

class ModemAccessPoint final : public AccessPoint {
  AE_OBJECT(ModemAccessPoint, AccessPoint, 0)
  ModemAccessPoint() = default;

 public:
  ModemAccessPoint(ObjPtr<Aether> aether, ModemAdapter::ptr modem_adapter,
                   Domain* domain);

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, modem_adapter_));

  ActionPtr<ModemConnectAction> Connect();
  IModemDriver& modem_driver();

  std::vector<ObjPtr<Channel>> GenerateChannels(
      std::vector<UnifiedAddress> const& endpoints) override;

 private:
  Obj::ptr aether_;
  ModemAdapter::ptr modem_adapter_;
};
}  // namespace ae

#endif  // AETHER_ACCESS_POINTS_MODEM_ACCESS_POINT_H_
