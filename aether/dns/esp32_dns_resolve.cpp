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

#include "aether/dns/esp32_dns_resolve.h"

#if defined ESP32_DNS_RESOLVER_ENABLED

#  include <map>
#  include <cstdint>
#  include <utility>

#  include "freertos/FreeRTOS.h"
#  include "freertos/task.h"
#  include "freertos/event_groups.h"
#  include "esp_system.h"
#  include "esp_event.h"
#  include "nvs_flash.h"

#  include "lwip/err.h"
#  include "lwip/sys.h"
#  include "lwip/dns.h"
#  include "lwip/tcpip.h"

#  include "aether/aether.h"
#  include "aether/actions/action_context.h"
#  include "aether/events/multi_subscription.h"

#  include "aether/dns/dns_tele.h"

namespace ae {

class GethostByNameDnsResolver {
  struct QueryContext {
    GethostByNameDnsResolver* self;
    ResolveAction resolve_action;
    NameAddress name_address;
  };

 public:
  explicit GethostByNameDnsResolver(Aether* aether)
      : action_context_{*aether->action_processor} {}

  ResolveAction& Query(NameAddress const& name_address) {
    static std::uint32_t query_id = 0;

    AE_TELE_DEBUG(DnsQueryHost, "Querying host: {}", name_address);

    auto [qit, _] = active_queries_.emplace(
        query_id++,
        QueryContext{this, ResolveAction{action_context_}, name_address});

    auto& query_context = qit->second;

    // remove self
    multi_subscription_.Push(
        query_context.resolve_action.FinishedEvent()
            .Subscribe([id{qit->first}, this]() { active_queries_.erase(id); })
            .Once());

    // make query
    ip_addr_t cached_addr;
    LOCK_TCPIP_CORE();
    auto res = dns_gethostbyname(
        name_address.name.c_str(), &cached_addr,
        [](const char* /* name */, const ip_addr_t* ipaddr,
           void* callback_arg) {
          auto* context = static_cast<QueryContext*>(callback_arg);
          context->self->QueryResult(*context, ipaddr);
        },
        &query_context);
    UNLOCK_TCPIP_CORE();

    if (res == ERR_OK) {
      QueryResult(query_context, &cached_addr);
    } else if (res == ERR_ARG) {
      AE_TELE_ERROR(DnsQueryError,
                    "Dns client not initialized or invalid hostname");
      query_context.resolve_action.Failed();
    }
    return query_context.resolve_action;
  }

 private:
  void QueryResult(QueryContext& query_context, ip_addr_t const* ipaddr) {
    if (!ipaddr) {
      query_context.resolve_action.Failed();
      return;
    }
    IpAddressPortProtocol addr;
    if (IP_IS_V4_VAL(*ipaddr)) {
      addr.ip.version = IpAddress::Version::kIpV4;
      auto ip4 = ip_2_ip4(ipaddr);
      addr.ip.set_value(reinterpret_cast<std::uint8_t const*>(&ip4->addr));
    } else if (IP_IS_V6_VAL(*ipaddr)) {
      addr.ip.version = IpAddress::Version::kIpV6;
      auto ip6 = ip_2_ip6(ipaddr);
      addr.ip.set_value(reinterpret_cast<std::uint8_t const*>(&ip6->addr));
    }
    addr.port = query_context.name_address.port;
    addr.protocol = query_context.name_address.protocol;

    AE_TELE_DEBUG(DnsQuerySuccess, "Got addresses {}", addr);
    query_context.resolve_action.SetAddress({std::move(addr)});
  }

  ActionContext action_context_;
  std::map<std::uint32_t, QueryContext> active_queries_;
  MultiSubscription multi_subscription_;
};

Esp32DnsResolver::Esp32DnsResolver() = default;

#  ifdef AE_DISTILLATION
Esp32DnsResolver::Esp32DnsResolver(ObjPtr<Aether> aether, Domain* domain)
    : DnsResolver{domain}, aether_{std::move(aether)} {}
#  endif

Esp32DnsResolver::~Esp32DnsResolver() = default;

ResolveAction& Esp32DnsResolver::Resolve(NameAddress const& name_address) {
  if (!gethostbyname_dns_resolver_) {
    gethostbyname_dns_resolver_ = std::make_unique<GethostByNameDnsResolver>(
        static_cast<Aether*>(aether_.get()));
  }
  return gethostbyname_dns_resolver_->Query(name_address);
}

}  // namespace ae

#endif
