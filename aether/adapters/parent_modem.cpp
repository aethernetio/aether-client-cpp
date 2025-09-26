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

#include "aether/adapters/parent_modem.h"

namespace ae {

#if defined AE_DISTILLATION
ParentModemAdapter::ParentModemAdapter(ObjPtr<Aether> aether,
                                       IPoller::ptr poller,
                                       ModemInit modem_init, Domain* domain)
    : Adapter{domain},
      aether_{std::move(aether)},
      poller_{std::move(poller)},
      modem_init_{std::move(modem_init)} {}
#endif  // AE_DISTILLATION

} /* namespace ae */
