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

#include "aether/dns/dns_c_ares.h"

#if defined DNS_RESOLVE_ARES_ENABLED

#  include <memory>
#  include <vector>
#  include <utility>

#  include "ares.h"

#  include "aether/warning_disable.h"

#  include "aether/aether.h"
#  include "aether/socket_initializer.h"
#  include "aether/executors/executors.h"

#  include "aether/dns/dns_tele.h"

namespace ae {
class AresImpl {
 public:
  explicit AresImpl() {
    ares_library_init(ARES_LIB_INIT_ALL);

    int optmask = ARES_OPT_EVENT_THREAD;
    ares_options options{};
    options.evsys = ARES_EVSYS_DEFAULT;

    /* Initialize channel to run queries, a single channel can accept unlimited
     * queries */
    if (auto res = ares_init_options(&channel_, &options, optmask);
        res != ARES_SUCCESS) {
      AE_TELE_ERROR(kAresDnsFailedInitialize,
                    "Failed to initialize ares options: {}",
                    ares_strerror(res));
      assert(false);
    }
  }

  ~AresImpl() {
    ares_destroy(channel_);
    ares_library_cleanup();
  }

  auto Query(NamedAddr const& name_address) {
    return ex::create<std::vector<Address>>(
        [this, name_address](auto& receiver) {
          AE_TELE_DEBUG(kAresDnsQueryHost, "Querying host: {}", name_address);
          ares_addrinfo_hints hints{};
          // BOTH ipv4 and ipv6
          hints.ai_family = AF_UNSPEC;
          hints.ai_flags = ARES_AI_CANONNAME;

          ares_getaddrinfo(
              channel_, name_address.name.c_str(), nullptr, &hints,
              [](void* arg, auto status, auto timeouts, auto result) {
                auto& recv = *static_cast<decltype(&receiver)>(arg);
                ae_defer[&] { ares_freeaddrinfo(result); };

                if (status != ARES_SUCCESS) {
                  AE_TELE_ERROR(kAresDnsQueryError, "Ares query error {} {}",
                                status, ares_strerror(status));
                  ex::set_error(std::move(recv), ex::make_error());
                  return;
                }
                auto addresses = ProcessResult(status, timeouts, result);
                ex::set_value(std::move(recv), std::move(addresses));
              },
              &receiver);
        });
  }

 private:
  static std::vector<Address> ProcessResult(int /* status */,
                                            int /* timeouts */,
                                            struct ares_addrinfo* result) {
    assert(result);

    std::vector<Address> addresses;
    for (auto* node = result->nodes; node != nullptr; node = node->ai_next) {
      if (node->ai_family == AF_INET) {
#  if AE_SUPPORT_IPV4
        IpV4Addr ipv4{};
        auto* ip4_addr = reinterpret_cast<struct sockaddr_in*>(node->ai_addr);
        std::memcpy(&ipv4.ipv4_value,
                    reinterpret_cast<std::uint8_t*>(&ip4_addr->sin_addr.s_addr),
                    sizeof(ipv4.ipv4_value));
        addresses.emplace_back(ipv4);
#  endif
      } else if (node->ai_family == AF_INET6) {
#  if AE_SUPPORT_IPV6
        IpV6Addr ipv6{};
        auto* ip6_addr = reinterpret_cast<struct sockaddr_in6*>(node->ai_addr);
        std::memcpy(
            &ipv6.ipv6_value,
            reinterpret_cast<std::uint8_t*>(&ip6_addr->sin6_addr.s6_addr),
            sizeof(ipv6.ipv6_value));
        addresses.emplace_back(ipv6);
#  endif
      }
    }

    AE_TELE_DEBUG(kAresDnsQuerySuccess, "Got addresses {}", addresses);
    return addresses;
  }

  ares_channel_t* channel_;

  AE_MAY_UNUSED_MEMBER SocketInitializer socket_initializer_;
};

DnsResolverCares::DnsResolverCares() = default;

#  if defined AE_DISTILLATION
DnsResolverCares::DnsResolverCares(ObjPtr<Aether> aether, Domain* domain)
    : DnsResolver(domain), aether_{std::move(aether)} {}
#  endif

DnsResolverCares::~DnsResolverCares() = default;

ex::any_sender_of<std::vector<Endpoint>> DnsResolverCares::Resolve(
    NamedAddr const& name_address, std::uint16_t port_hint,
    Protocol protocol_hint) {
  if (!ares_impl_) {
    ares_impl_ = std::make_unique<AresImpl>();
  }
  return ares_impl_->Query(name_address) |
         ex::then([port_hint,
                   protocol_hint](std::vector<Address> const& addresses) {
           std::vector<Endpoint> endpoints;
           endpoints.reserve(addresses.size());
           for (auto const& addr : addresses) {
             endpoints.emplace_back(Endpoint{{addr, port_hint}, protocol_hint});
           }
           return endpoints;
         });
}

}  // namespace ae

#endif
