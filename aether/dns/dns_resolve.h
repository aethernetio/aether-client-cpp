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

#ifndef AETHER_DNS_DNS_RESOLVE_H_
#define AETHER_DNS_DNS_RESOLVE_H_

#include <vector>
#include <cassert>

#include "aether/config.h"
#include "aether/obj/obj.h"
#include "aether/obj/dummy_obj.h"
#if AE_SUPPORT_CLOUD_DNS
#  include "aether/types/address.h"
#  include "aether/actions/action.h"
#  include "aether/actions/action_ptr.h"

namespace ae {
// Action to get host name resolve
class ResolveAction : public Action<ResolveAction> {
 public:
  using Action::Action;

  UpdateStatus Update() const;

  // Set ip addresses after resolve query finished
  void SetAddress(std::vector<IpAddressPortProtocol> addr);
  void Failed();

  bool is_resolved{false};
  bool is_failed{false};
  std::vector<IpAddressPortProtocol> addresses;
};

/**
 * \brief Base class of DNS resolver
 */
class DnsResolver : public Obj {
  AE_OBJECT(DnsResolver, Obj, 0)

 protected:
  DnsResolver() = default;

 public:
#  if defined AE_DISTILLATION
  explicit DnsResolver(Domain* domain) : Obj{domain} {}
#  endif
  ~DnsResolver() override = default;

  AE_OBJECT_REFLECT()

  // Make a host name resolve
  virtual ActionPtr<ResolveAction> Resolve(NameAddress const& name_address);
};
}  // namespace ae
#else
namespace ae {
class DnsResolver : public DummyObj {
  AE_OBJECT(DnsResolver, DummyObj, 0)
  DnsResolver() = default;

 public:
  AE_OBJECT_REFLECT()
};
}  // namespace ae
#endif
#endif  // AETHER_DNS_DNS_RESOLVE_H_
