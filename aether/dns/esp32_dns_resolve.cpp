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

#  include <cstdint>
#  include <utility>

// #  include "freertos/FreeRTOS.h"
// #  include "freertos/task.h"
// #  include "freertos/event_groups.h"

#  include "lwip/err.h"
#  include "lwip/dns.h"
#  include "lwip/tcpip.h"

#  include "aether/aether.h"
#  include "aether/executors/executors.h"

#  include "aether/dns/dns_tele.h"

namespace ae {
Result<std::vector<Endpoint>, int> ProcessQueryResult(ip_addr_t const* ipaddr,
                                                      std::uint16_t port,
                                                      Protocol protocol) {
  if (ipaddr == nullptr) {
    return Error{1};
  }

  std::vector<Endpoint> resolved_addresses;

  auto addr_add = [&](auto const& ip) {
    resolved_addresses.emplace_back(Endpoint{{ip, port}, protocol});
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
  AE_TELE_DEBUG(kEspDnsQuerySuccess, "Got addresses {}", resolved_addresses);
  return Ok{std::move(resolved_addresses)};
}

class GHbyNameResolveSender {
 public:
  struct State {
    NamedAddr name_address;
    std::uint16_t port_hint;
    Protocol protocol_hint;
  };

  template <typename R>
  struct Operation {
    void start() noexcept {
      AE_TELE_DEBUG(kEspDnsQueryHost, "Querying host: {}", state.name_address);

      // make query
      ip_addr_t cached_addr;
      LOCK_TCPIP_CORE();
      auto res = dns_gethostbyname(
          state.name_address.name.c_str(), &cached_addr,
          [](const char* /* name */, const ip_addr_t* ipaddr, void* arg) {
            auto* op = static_cast<Operation<R>*>(arg);
            auto r = ProcessQueryResult(ipaddr, op->state.port_hint,
                                        op->state.protocol_hint);
            if (r.IsOk()) {
              ex::set_value(std::move(op->recv), std::move(r.value()));
            } else {
              ex::set_error(std::move(op->recv), std::move(r.error()));
            }
          },
          this);
      UNLOCK_TCPIP_CORE();

      if (res == ERR_OK) {
        // process cached value
        auto r = ProcessQueryResult(&cached_addr, state.port_hint,
                                    state.protocol_hint);
        if (r.IsOk()) {
          ex::set_value(std::move(recv), std::move(r.value()));
        } else {
          ex::set_error(std::move(recv), std::move(r.error()));
        }
      } else if (res == ERR_ARG) {
        AE_TELE_ERROR(kEspDnsQueryError,
                      "Dns client not initialized or invalid hostname");
        ex::set_error(std::move(recv), 2);
      }
    }

    R recv;
    State state;
  };

  using sender_concept = ex::sender_t;

  constexpr GHbyNameResolveSender(NamedAddr const& name_address,
                                  std::uint16_t port_hint,
                                  Protocol protocol_hint) noexcept
      : state_{
            .name_address = name_address,
            .port_hint = port_hint,
            .protocol_hint = protocol_hint,
        } {}

  template <typename...>
  static consteval auto get_completion_signatures() noexcept {
    return ex::completion_signatures<ex::set_value_t(std::vector<Endpoint>),
                                     ex::set_error_t(int)>{};
  }

  template <ex::receiver R>
  constexpr auto connect(R&& r) && noexcept {
    return Operation{.recv = std::forward<R>(r), .state = std::move(state_)};
  }

 private:
  State state_;
};

Esp32DnsResolver::Esp32DnsResolver(ObjProp prop, ObjPtr<Aether> aether)
    : DnsResolver{prop}, aether_{std::move(aether)} {}

ResolveSender Esp32DnsResolver::Resolve(NamedAddr const& name_address,
                                        std::uint16_t port_hint,
                                        Protocol protocol_hint) {
  return GHbyNameResolveSender{name_address, port_hint, protocol_hint} |
         ex::continues_on(ex::SchedulerOnTasks{*aether_.Load().as<Aether>()});
}

}  // namespace ae

#endif
