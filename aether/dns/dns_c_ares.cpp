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
#  include "aether/actions/action_ptr.h"
#  include "aether/actions/action_context.h"

#  include "aether/dns/dns_tele.h"

namespace ae {
class AresQueryAction : public Action<AresQueryAction> {
 public:
  AresQueryAction(ActionContext action_context, NameAddress name_address)
      : Action{action_context}, name_address_{std::move(name_address)} {}

  UpdateStatus Update() {
    if (is_result_) {
      return UpdateStatus::Result();
    }
    if (is_failed_) {
      return UpdateStatus::Error();
    }
    return {};
  }

  void ProcessResult(int status, int /* timeouts */,
                     struct ares_addrinfo* result) {
    if (status != ARES_SUCCESS) {
      AE_TELE_ERROR(kAresDnsQueryError, "Ares query error {} {}", status,
                    ares_strerror(status));
      Failed();
      return;
    }
    assert(result);

    for (auto* node = result->nodes; node != nullptr; node = node->ai_next) {
      auto& addr = resolved_addresses_.emplace_back();

      if (node->ai_family == AF_INET) {
        addr.ip.version = IpAddress::Version::kIpV4;
        auto* ip4_addr = reinterpret_cast<struct sockaddr_in*>(node->ai_addr);
        addr.ip.set_value(
            reinterpret_cast<std::uint8_t*>(&ip4_addr->sin_addr.s_addr));
      } else if (node->ai_family == AF_INET6) {
        addr.ip.version = IpAddress::Version::kIpV6;
        auto* ip6_addr = reinterpret_cast<struct sockaddr_in6*>(node->ai_addr);
        addr.ip.set_value(
            reinterpret_cast<std::uint8_t*>(&ip6_addr->sin6_addr.s6_addr));
      }
      addr.port = name_address_.port;
      addr.protocol = name_address_.protocol;
    }

    AE_TELE_DEBUG(kAresDnsQuerySuccess, "Got addresses {}",
                  resolved_addresses_);
    Result();
  }

  std::vector<IpAddressPortProtocol> const& resolved_addresses() const {
    return resolved_addresses_;
  }

 private:
  void Result() {
    is_result_ = true;
    Action::Trigger();
  }
  void Failed() {
    is_failed_ = true;
    Action::Trigger();
  }

  NameAddress name_address_;
  std::vector<IpAddressPortProtocol> resolved_addresses_;
  std::atomic_bool is_result_{false};
  std::atomic_bool is_failed_{false};
};

class AresImpl {
 public:
  struct QueryContext {
    AresImpl* self;
    ResolveAction resolve_action;
    NameAddress name_address;
  };

  explicit AresImpl(ActionContext action_context)
      : action_context_{action_context} {
    ares_library_init(ARES_LIB_INIT_ALL);

    // ares and win_poller are not compatible
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

  ActionPtr<ResolveAction> Query(NameAddress const& name_address) {
    AE_TELE_DEBUG(kAresDnsQueryHost, "Querying host: {}", name_address);

    auto resolve_action = ActionPtr<ResolveAction>{action_context_};
    auto query_action =
        ActionPtr<AresQueryAction>{action_context_, name_address};

    // connect actions
    multi_subscription_.Push(
        query_action->StatusEvent().Subscribe(ActionHandler{
            OnResult{[ra{resolve_action}](auto const& action) mutable {
              ra->SetAddress(action.resolved_addresses());
            }},
            OnError{[ra{resolve_action}]() mutable { ra->Failed(); }},
        }));

    ares_addrinfo_hints hints{};
    // BOTH ipv4 and ipv6
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = ARES_AI_CANONNAME;

    ares_getaddrinfo(
        channel_, name_address.name.c_str(), nullptr, &hints,
        [](void* arg, auto status, auto timeouts, auto result) {
          auto& ares_query_action = *static_cast<AresQueryAction*>(arg);
          ares_query_action.ProcessResult(status, timeouts, result);
          ares_freeaddrinfo(result);
        },
        &*query_action);

    return resolve_action;
  }

 private:
  ActionContext action_context_;
  ares_channel_t* channel_;

  MultiSubscription multi_subscription_;
  AE_MAY_UNUSED_MEMBER SocketInitializer socket_initializer_;
};

DnsResolverCares::DnsResolverCares() = default;

#  if defined AE_DISTILLATION
DnsResolverCares::DnsResolverCares(ObjPtr<Aether> aether, Domain* domain)
    : DnsResolver(domain), aether_{std::move(aether)} {}
#  endif

DnsResolverCares::~DnsResolverCares() = default;

ActionPtr<ResolveAction> DnsResolverCares::Resolve(
    NameAddress const& name_address) {
  if (!ares_impl_) {
    ares_impl_ = std::make_unique<AresImpl>(*aether_.as<Aether>());
  }
  return ares_impl_->Query(name_address);
}

}  // namespace ae

#endif
