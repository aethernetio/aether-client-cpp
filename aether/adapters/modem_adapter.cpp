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
#include "aether/adapters/adapter_tele.h"

namespace ae {
  
#  if defined AE_DISTILLATION
ModemAdapter::ModemAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
                                   ModemInit modem_init,
                                   Domain* domain)
    : ParentModemAdapter{std::move(aether), std::move(poller), std::move(modem_init),
                        domain} {
  AE_TELED_DEBUG("Modem instance created!");
}

ModemAdapter::~ModemAdapter() {
  if (connected_ == true) {
    DisConnect();
    AE_TELE_DEBUG(kAdapterDestructor, "Modem instance deleted!");
    connected_ = false;
  }
}
#  endif  // AE_DISTILLATION

}  // namespace ae