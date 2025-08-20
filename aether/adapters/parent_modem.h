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

#ifndef AETHER_ADAPTERS_PARENT_MODEM_H_
#define AETHER_ADAPTERS_PARENT_MODEM_H_

#include "aether/aether.h"
#include "aether/adapters/adapter.h"
#include "aether/modems/modem_driver_types.h"

namespace ae {
class Aether;

class ParentModemAdapter : public Adapter {
  AE_OBJECT(ParentModemAdapter, Adapter, 0)

 protected:
  ParentModemAdapter() = default;

 public:
#ifdef AE_DISTILLATION
  ParentModemAdapter(ObjPtr<Aether> aether, ModemInit modem_init,
                     Domain* domain);
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, modem_init_))

  Obj::ptr aether_;

  ModemInit modem_init_;
};
}  // namespace ae

#endif  // AETHER_ADAPTERS_PARENT_MODEM_H_
