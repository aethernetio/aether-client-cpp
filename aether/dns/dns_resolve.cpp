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

#include "aether/dns/dns_resolve.h"

#if AE_SUPPORT_CLOUD_DNS
#  include <utility>

namespace ae {

TimePoint ResolveAction::Update(TimePoint current_time) {
  if (is_resolved) {
    is_resolved = false;
    Action::Result(*this);
  } else if (is_failed) {
    is_failed = false;
    Action::Error(*this);
  }
  return current_time;
}

void ResolveAction::SetAddress(std::vector<IpAddressPortProtocol> addr) {
  addresses = std::move(addr);
  is_resolved = true;
  this->Trigger();
}

void ResolveAction::Failed() {
  is_failed = true;
  is_resolved = false;
  this->Trigger();
}

ResolveAction& DnsResolver::Resolve(NameAddress const& /* name_address */) {
  // must be overriden
  assert(false);
  std::abort();
}

}  // namespace ae
#endif
