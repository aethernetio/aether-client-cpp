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

#ifndef AETHER_DNS_ESP32_DNS_RESOLVE_H_
#define AETHER_DNS_ESP32_DNS_RESOLVE_H_

#include "aether/config.h"
#if AE_SUPPORT_CLOUD_DNS

#  include "aether/env.h"
#  if (defined(ESP_PLATFORM))
#    define ESP32_DNS_RESOLVER_ENABLED 1

#    include <memory>

#    include "aether/dns/dns_resolve.h"
#    include "aether/obj/obj.h"

namespace ae {
class Aether;
class GethostByNameDnsResolver;

class Esp32DnsResolver : public DnsResolver {
  AE_OBJECT(Esp32DnsResolver, DnsResolver, 0)

  Esp32DnsResolver();

 public:
#    ifdef AE_DISTILLATION
  Esp32DnsResolver(ObjPtr<Aether> aether, Domain* domain);
#    endif

  ~Esp32DnsResolver() override;

  AE_OBJECT_REFLECT(AE_MMBR(aether_))

  ResolveAction& Resolve(NameAddress const& name_address) override;

 private:
  Obj::ptr aether_;
  std::unique_ptr<GethostByNameDnsResolver> gethostbyname_dns_resolver_;
};
}  // namespace ae

#  endif
#endif
#endif  // AETHER_DNS_ESP32_DNS_RESOLVE_H_
