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

#include "aether/adapters/modem_adapter.h"

#include "aether/aether.h"
#include "aether/modems/modem_factory.h"
#include "aether/access_points/modem_access_point.h"

#include "aether/adapters/adapter_tele.h"

namespace ae {

#if defined AE_DISTILLATION
ModemAdapter::ModemAdapter(ObjPtr<Aether> aether, ModemInit modem_init,
                           Domain* domain)
    : ParentModemAdapter{std::move(aether), std::move(modem_init), domain} {
  AE_TELED_DEBUG("Modem instance created!");
}
#endif  // AE_DISTILLATION

ModemAdapter::~ModemAdapter() {
  if (connected_) {
    AE_TELED_DEBUG("Modem instance deleted!");
    connected_ = false;
  }
}

std::vector<AccessPoint::ptr> ModemAdapter::access_points() {
  if (!access_point_) {
    Aether::ptr aether = aether_;
    ModemAdapter::ptr self = MakePtrFromThis(this);
    access_point_ = domain_->CreateObj<ModemAccessPoint>(aether, self);
  }
  return {access_point_};
}

IModemDriver& ModemAdapter::modem_driver() {
  if (!modem_driver_) {
    modem_driver_ = ModemDriverFactory::CreateModem(modem_init_);
  }
  return *modem_driver_;
}

}  // namespace ae
