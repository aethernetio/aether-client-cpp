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
#  include "aether/actions/action_ptr.h"
#  include "aether/actions/action_context.h"
#  include "aether/events/multi_subscription.h"

#  include "aether/dns/dns_tele.h"

namespace ae {

class GetHostByNameQueryAction : public Action<GetHostByNameQueryAction> {
 public:
  GetHostByNameQueryAction(ActionContext action_context,
                           NamedAddr const& name_address, std::uint16_t port,
                           Protocol protocol)
      : Action{action_context},
        name_address_{std::move(name_address)},
        port_{port},
        protocol_{protocol} {}

  UpdateStatus Update() {
    if (is_result_) {
      return UpdateStatus::Result();
    }
    if (is_failed_) {
      return UpdateStatus::Error();
    }
    return {};
  }

  void ProcessQueryResult(ip_addr_t const* ipaddr) {
    if (!ipaddr) {
      Failed();
      return;
    }
    auto addr_add = [this](auto const& ip) {
      Endpoint addr{{ip, port_}, protocol_};
      resolved_addresses_.emplace_back(std::move(addr));
    };

    if (IP_IS_V4_VAL(*ipaddr)) {
#  if AE_SUPPORT_IPV4
      IpV4Addr ipv4{};
      auto ip4 = ip_2_ip4(ipaddr);
      std::memcpy(&ipv4.ipv4_value,
                  reinterpret_cast<std::uint8_t const*>(&ip4->addr),
                  sizeof(ipv4.ipv4_value));
      addr_add(ipv4);
#  endif
    } else if (IP_IS_V6_VAL(*ipaddr)) {
#  if AE_SUPPORT_IPV6
      IpV6Addr ipv6{};
      auto ip6 = ip_2_ip6(ipaddr);
      addr.ip.set_value(reinterpret_cast<std::uint8_t const*>(&ip6->addr));
      std::memcpy(&ipv6.ipv6_value,
                  reinterpret_cast<std::uint8_t const*>(&ip6->addr),
                  sizeof(ipv6.ipv6_value));
      addr_add(ipv6);
#  endif
    }
    AE_TELE_DEBUG(kEspDnsQuerySuccess, "Got addresses {}", resolved_addresses_);
    Result();
  }

  std::vector<Endpoint> const& resolved_addresses() const {
    return resolved_addresses_;
  }

  void Failed() {
    is_failed_ = true;
    Action::Trigger();
  }

 private:
  void Result() {
    is_result_ = true;
    Action::Trigger();
  }

  NamedAddr name_address_;
  std::uint16_t port_;
  Protocol protocol_;
  std::vector<Endpoint> resolved_addresses_;
  std::atomic_bool is_result_{false};
  std::atomic_bool is_failed_{false};
};

class GethostByNameDnsResolver {
 public:
  explicit GethostByNameDnsResolver(ActionContext action_context)
      : action_context_{action_context} {}

  ActionPtr<ResolveAction> Query(NamedAddr const& name_address,
                                 std::uint16_t port_hint,
                                 Protocol protocol_hint) {
    AE_TELE_DEBUG(kEspDnsQueryHost, "Querying host: {}", name_address);
    auto resolve_action = ActionPtr<ResolveAction>{action_context_};
    auto query_action = ActionPtr<GetHostByNameQueryAction>{
        action_context_, name_address, port_hint, protocol_hint};

    // connect actions
    multi_subscription_.Push(
        query_action->StatusEvent().Subscribe(ActionHandler{
            OnResult{[ra{resolve_action}](auto const& action) mutable {
              ra->SetAddress(action.resolved_addresses());
            }},
            OnError{
                [ra{resolve_action}](auto const&) mutable { ra->Failed(); }},
        }));

    // make query
    ip_addr_t cached_addr;
    LOCK_TCPIP_CORE();
    auto res = dns_gethostbyname(
        name_address.name.c_str(), &cached_addr,
        [](const char* /* name */, const ip_addr_t* ipaddr,
           void* callback_arg) {
          auto* query_action =
              static_cast<GetHostByNameQueryAction*>(callback_arg);
          query_action->ProcessQueryResult(ipaddr);
        },
        &*query_action);
    UNLOCK_TCPIP_CORE();

    if (res == ERR_OK) {
      query_action->ProcessQueryResult(&cached_addr);
    } else if (res == ERR_ARG) {
      AE_TELE_ERROR(kEspDnsQueryError,
                    "Dns client not initialized or invalid hostname");
      query_action->Failed();
    }
    return resolve_action;
  }

 private:
  ActionContext action_context_;
  MultiSubscription multi_subscription_;
};

Esp32DnsResolver::Esp32DnsResolver() = default;

#  ifdef AE_DISTILLATION
Esp32DnsResolver::Esp32DnsResolver(ObjProp prop, ObjPtr<Aether> aether)
    : DnsResolver{prop}, aether_{std::move(aether)} {}
#  endif

Esp32DnsResolver::~Esp32DnsResolver() = default;

ActionPtr<ResolveAction> Esp32DnsResolver::Resolve(
    NamedAddr const& name_address, std::uint16_t port_hint,
    Protocol protocol_hint) {
  if (!gethostbyname_dns_resolver_) {
    gethostbyname_dns_resolver_ = std::make_unique<GethostByNameDnsResolver>(
        *aether_.Load().as<Aether>());
  }
  return gethostbyname_dns_resolver_->Query(name_address, port_hint,
                                            protocol_hint);
}

}  // namespace ae

#endif
