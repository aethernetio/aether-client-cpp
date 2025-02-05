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

#ifndef AETHER_DNS_DNS_C_ARES_H_
#define AETHER_DNS_DNS_C_ARES_H_

#include "aether/config.h"

#if AE_SUPPORT_CLOUD_DNS

#  if defined __linux__ || defined __ANDROID__ || defined __APPLE__ || \
      defined __FreeBSD__ || defined _WIN32

#    define DNS_RESOLVE_ARES_ENABLED 1

#    include <memory>

#    include "aether/dns/dns_resolve.h"

namespace ae {
class Aether;
class AresImpl;

class DnsResolverCares : public DnsResolver {
  AE_OBJECT(DnsResolverCares, DnsResolver, 0)

  DnsResolverCares();

 public:
#    if defined AE_DISTILLATION
  DnsResolverCares(ObjPtr<Aether> aether, Domain* domain);
#    endif
  ~DnsResolverCares() override;

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
    dnv(aether_);
  }

  ResolveAction& Resolve(NameAddress const& name_address) override;

 private:
  Obj::ptr aether_;
  std::unique_ptr<AresImpl> ares_impl_;
};
}  // namespace ae
#  endif
#endif
#endif  // AETHER_DNS_DNS_C_ARES_H_
