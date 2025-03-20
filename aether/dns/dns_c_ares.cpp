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

#  include <set>
#  include <map>
#  include <memory>
#  include <vector>
#  include <utility>

#  include "ares.h"

#  include "aether/warning_disable.h"

#  include "aether/aether.h"
#  include "aether/poller/poller.h"
#  include "aether/socket_initializer.h"
#  include "aether/actions/action_context.h"

#  include "aether/dns/dns_tele.h"

namespace ae {
class AresImpl {
 public:
  struct QueryContext {
    AresImpl* self;
    ResolveAction resolve_action;
    NameAddress name_address;
  };

  explicit AresImpl(Aether* aether)
      : action_context_{*aether->action_processor}, poller_{aether->poller} {
    assert(poller_);

    ares_library_init(ARES_LIB_INIT_ALL);

#  if defined WIN32
    // ares and win_poller are not compatible
    int optmask = ARES_OPT_EVENT_THREAD;
    ares_options options{};
    options.evsys = ARES_EVSYS_DEFAULT;
#  else
    int optmask = ARES_OPT_SOCK_STATE_CB;
    ares_options options{};
    options.sock_state_cb = [](void* data, auto socket_fd, auto readable,
                               auto writable) {
      auto& ares_impl = *static_cast<AresImpl*>(data);
      ares_impl.SocketState(socket_fd, readable, writable);
    };
    options.sock_state_cb_data = this;
#  endif

    /* Initialize channel to run queries, a single channel can accept unlimited
     * queries */
    if (auto res = ares_init_options(&channel_, &options, optmask);
        res != ARES_SUCCESS) {
      AE_TELE_ERROR(DnsCAresFailedInitialize,
                    "Failed to initialize ares options: {}",
                    ares_strerror(res));
      assert(false);
    }
  }

  ~AresImpl() {
    ares_destroy(channel_);
    ares_library_cleanup();
#  if !defined WIN32
    for (auto sock : opened_sockets_) {
      poller_->Remove(sock);
    }
#  endif
  }

  ResolveAction& Query(NameAddress const& name_address) {
    static std::uint32_t query_id = 0;

    AE_TELE_DEBUG(DnsQueryHost, "Querying host: {}", name_address);

    auto [qit, _] = active_queries_.emplace(
        query_id++,
        QueryContext{this, ResolveAction{action_context_}, name_address});

    auto& q_context = qit->second;

    // remove itself
    multi_subscription_.Push(
        q_context.resolve_action.FinishedEvent()
            .Subscribe([id{qit->first}, this]() { active_queries_.erase(id); })
            .Once());

    ares_addrinfo_hints hints{};
    // BOTH ipv4 and ipv6
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = ARES_AI_CANONNAME;

    ares_getaddrinfo(
        channel_, name_address.name.c_str(), nullptr, &hints,
        [](void* arg, auto status, auto timeouts, auto result) {
          auto& query_context = *static_cast<QueryContext*>(arg);
          QueryResult(query_context, status, timeouts, result);
          ares_freeaddrinfo(result);
        },
        &q_context);

    return q_context.resolve_action;
  }

 private:
#  if !defined WIN32
  void SocketState(ares_socket_t socket_fd, int readable, int writable) {
    auto [it, inserted] = opened_sockets_.insert(socket_fd);
    if (!inserted) {
      if ((readable == 0) && (writable == 0)) {
        // remove socket
        poller_->Remove(socket_fd);
        opened_sockets_.erase(it);
      }
      return;
    }

    poll_sockets_subscriptions_.Push(poller_->Add(socket_fd).Subscribe(
        *this, MethodPtr<&AresImpl::SocketEvent>{}));
  }

  void SocketEvent(PollerEvent event) {
    auto it = opened_sockets_.find(event.descriptor);
    if (it == std::end(opened_sockets_)) {
      return;
    }
    switch (event.event_type) {
      case EventType::kRead:
        ares_process_fd(channel_, event.descriptor, ARES_SOCKET_BAD);
        break;
      case EventType::kWrite:
        ares_process_fd(channel_, ARES_SOCKET_BAD, event.descriptor);
        break;
      case EventType::kError:
        ares_process_fd(channel_, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
        poller_->Remove(event.descriptor);
        opened_sockets_.erase(it);
        break;
    }
  }

#  endif

  static void QueryResult(QueryContext& context, int status, int /* timeouts */,
                          struct ares_addrinfo* result) {
    if (status != ARES_SUCCESS) {
      AE_TELE_ERROR(DnsQueryError, "Ares query error {} {}", status,
                    ares_strerror(status));
      context.resolve_action.Failed();
      return;
    }
    assert(result);

    std::vector<IpAddressPortProtocol> addresses;

    for (auto* node = result->nodes; node != nullptr; node = node->ai_next) {
      auto& addr = addresses.emplace_back();

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
      addr.port = context.name_address.port;
      addr.protocol = context.name_address.protocol;
    }

    AE_TELE_DEBUG(DnsQuerySuccess, "Got addresses {}", addresses);
    context.resolve_action.SetAddress(std::move(addresses));
  }

  ActionContext action_context_;
  IPoller::ptr poller_;
  ares_channel_t* channel_;
  AE_MAY_UNUSED_MEMBER std::set<ares_socket_t> opened_sockets_;
  std::map<std::uint32_t, QueryContext> active_queries_;

  MultiSubscription multi_subscription_;
  AE_MAY_UNUSED_MEMBER MultiSubscription poll_sockets_subscriptions_;
  AE_MAY_UNUSED_MEMBER SocketInitializer socket_initializer_;
};

DnsResolverCares::DnsResolverCares() = default;

#  if defined AE_DISTILLATION
DnsResolverCares::DnsResolverCares(ObjPtr<Aether> aether, Domain* domain)
    : DnsResolver(domain), aether_{std::move(aether)} {}
#  endif

DnsResolverCares::~DnsResolverCares() = default;

ResolveAction& DnsResolverCares::Resolve(NameAddress const& name_address) {
  if (!ares_impl_) {
    ares_impl_ = std::make_unique<AresImpl>(aether_.as<Aether>());
  }
  return ares_impl_->Query(name_address);
}

}  // namespace ae

#endif
