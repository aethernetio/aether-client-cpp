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
#  include "aether/types/result.h"
#  include "aether/socket_initializer.h"
#  include "aether/events/multi_subscription.h"

#  include "aether/executors/executors.h"

#  include "aether/dns/dns_tele.h"

namespace ae {
class AresImpl {
  struct QueryContext {
    AresImpl* ares_impl;
    NamedAddr name_address;
    std::uint16_t port_hint;
    Protocol protocol_hint;
  };

  template <typename R>
  struct Operation {
    static void Callback(void* arg, int status, int timeouts,
                         struct ares_addrinfo* result) {
      auto* op = static_cast<Operation<R>*>(arg);
      auto r = AresImpl::ProcessResult(
          status, timeouts, result, op->ctx.port_hint, op->ctx.protocol_hint);
      // set either value or error
      if (r.IsOk()) {
        ex::set_value(std::move(op->recv), std::move(r.value()));
      } else {
        ex::set_error(std::move(op->recv), r.error());
      }
    }

    void start() & noexcept {
      AE_TELE_DEBUG(kAresDnsQueryHost, "Querying host: {}", ctx.name_address);

      ares_addrinfo_hints hints{};
      // BOTH ipv4 and ipv6
      hints.ai_family = AF_UNSPEC;
      hints.ai_flags = ARES_AI_CANONNAME;
      ares_getaddrinfo(ctx.ares_impl->channel_, ctx.name_address.name.c_str(),
                       nullptr, &hints, Callback, this);
    }

    R recv;
    QueryContext ctx;
  };

  struct Sender {
    using sender_concept = stdexec::sender_t;
    using completion_signatures =
        stdexec::completion_signatures<ex::set_value_t(std::vector<Endpoint>&&),
                                       ex::set_error_t(int)>;

    template <typename R>
    auto connect(R&& r) && noexcept {
      return Operation<R>{.recv = std::forward<R>(r), .ctx = std::move(ctx)};
    }

    QueryContext ctx;
  };

 public:
  AresImpl() {
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

  Sender Query(NamedAddr const& name_address, std::uint16_t port_hint,
               Protocol protocol_hint) {
    return Sender{.ctx{.ares_impl = this,
                       .name_address = name_address,
                       .port_hint = port_hint,
                       .protocol_hint = protocol_hint}};
  }

  static Result<std::vector<Endpoint>, int> ProcessResult(
      int status, int /* timeouts */, struct ares_addrinfo* result,
      std::uint16_t port_hint, Protocol protocol_hint) noexcept {
    if (status != ARES_SUCCESS) {
      AE_TELE_ERROR(kAresDnsQueryError, "Ares query error {} {}", status,
                    ares_strerror(status));
      return Error{1};
    }
    assert(result != nullptr);
    ae_defer[result]() { ares_freeaddrinfo(result); };

    std::vector<Endpoint> resolved_addresses;
    for (auto* node = result->nodes; node != nullptr; node = node->ai_next) {
      auto addr_add = [&](auto const& ip) {
        Endpoint addr{{ip, port_hint}, protocol_hint};
        resolved_addresses.emplace_back(std::move(addr));
      };

      if (node->ai_family == AF_INET) {
#  if AE_SUPPORT_IPV4
        IpV4Addr ipv4{};
        auto* ip4_addr = reinterpret_cast<struct sockaddr_in*>(node->ai_addr);
        std::memcpy(&ipv4.ipv4_value,
                    reinterpret_cast<std::uint8_t*>(&ip4_addr->sin_addr.s_addr),
                    sizeof(ipv4.ipv4_value));
        addr_add(ipv4);
#  endif
      } else if (node->ai_family == AF_INET6) {
#  if AE_SUPPORT_IPV6
        IpV6Addr ipv6{};
        auto* ip6_addr = reinterpret_cast<struct sockaddr_in6*>(node->ai_addr);
        std::memcpy(
            &ipv6.ipv6_value,
            reinterpret_cast<std::uint8_t*>(&ip6_addr->sin6_addr.s6_addr),
            sizeof(ipv6.ipv6_value));
        addr_add(ipv6);
#  endif
      }
    }

    AE_TELE_DEBUG(kAresDnsQuerySuccess, "Got addresses {}", resolved_addresses);
    return Ok{std::move(resolved_addresses)};
  }

 private:
  ares_channel_t* channel_;

  MultiSubscription multi_subscription_;
  AE_MAY_UNUSED_MEMBER SocketInitializer socket_initializer_;
};

DnsResolverCares::DnsResolverCares() = default;

#  if defined AE_DISTILLATION
DnsResolverCares::DnsResolverCares(ObjProp prop, ObjPtr<Aether> aether)
    : DnsResolver{prop}, aether_{std::move(aether)} {}
#  endif

DnsResolverCares::~DnsResolverCares() = default;

struct minimal_sender {
  using sender_concept = ex::sender_t;

  // 1. Declare what this sender is capable of returning
  using completion_signatures = ex::completion_signatures<ex::set_value_t(
      int)  // We will call set_value with an int
                                                          >;

  // 2. Define the Operation State (the actual execution logic)
  template <typename Receiver>
  struct operation_state {
    Receiver rcvr;

    // The "Start" method is where the work actually happens
    void start() noexcept { stdexec::set_value(std::move(rcvr), 1); }
  };

  // 3. Connect the blueprint (Sender) to a destination (Receiver)
  template <stdexec::receiver Receiver>
  auto connect(Receiver rcvr) const {
    return operation_state<Receiver>{std::move(rcvr)};
  }
};

ResolveSender DnsResolverCares::Resolve(NamedAddr const& name_address,
                                        std::uint16_t port_hint,
                                        Protocol protocol_hint) {
  if (!ares_impl_) {
    ares_impl_ = std::make_unique<AresImpl>();
  }

  return ares_impl_->Query(name_address, port_hint, protocol_hint) |
         ex::continues_on(ex::SchedulerOnTasks{*aether_.Load().as<Aether>()});
}

}  // namespace ae

#endif
