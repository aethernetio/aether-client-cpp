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

#include "aether/transport/actions/name_address_channel_connection.h"

#if AE_SUPPORT_CLOUD_DNS

#  include <utility>

#  include "aether/aether.h"

#  include "aether/transport/itransport.h"
#  include "aether/transport/actions/ip_channel_connection.h"

#  include "aether/tele/tele.h"

namespace ae {
NameAddressChannelConnectionAction::NameAddressChannelConnectionAction(
    ActionContext action_context, NameAddress name_address,
    Ptr<Aether> const& aether, Ptr<Adapter> const& adapter)
    : ChannelConnectionAction(action_context),
      name_address_{std::move(name_address)},
      action_context_{action_context},
      adapter_{adapter},
      state_{State::Start} {
  if (!aether->dns_resolver) {
    aether->domain_->LoadRoot(aether->dns_resolver);
  }
  assert(aether->dns_resolver);
  dns_resolver_ = aether->dns_resolver;

  state_changed_subscription_ =
      state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
}

NameAddressChannelConnectionAction::~NameAddressChannelConnectionAction() =
    default;

TimePoint NameAddressChannelConnectionAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::Start:
        NameResolve(current_time);
        break;
      case State::TryConnection:
        TryConnection(current_time);
        break;
      case State::Connected:
        Action::Result(*this);
        break;
      case State::NotConnected:
        Action::Error(*this);
        break;
      default:
        break;
    }
  }
  return current_time;
}

std::unique_ptr<ITransport> NameAddressChannelConnectionAction::transport() {
  return std::move(transport_);
}

ConnectionInfo NameAddressChannelConnectionAction::connection_info() const {
  return connection_info_;
}

void NameAddressChannelConnectionAction::NameResolve(
    TimePoint /* current_time */) {
  auto resolver_ptr = dns_resolver_.Lock();
  auto& resolver_action = resolver_ptr->Resolve(name_address_);
  dns_resolve_subscriptions_.Push(
      resolver_action.ResultEvent().Subscribe([this](auto const& action) {
        ip_address_port_protocols_ = action.addresses;
        ip_address_port_protocol_it_ = std::begin(ip_address_port_protocols_);
        state_.Set(State::TryConnection);
      }),
      resolver_action.ErrorEvent().Subscribe(
          [this](auto const&) { state_.Set(State::NotConnected); }));
}

void NameAddressChannelConnectionAction::TryConnection(
    TimePoint /* current_time */) {
  if (ip_address_port_protocol_it_ == std::end(ip_address_port_protocols_)) {
    state_.Set(State::NotConnected);
    return;
  }

  auto adapter = adapter_.Lock();
  if (!adapter) {
    AE_TELED_ERROR("Adapter is null");
    state_.Set(State::NotConnected);
    return;
  }

  ip_address_channel_connection_.emplace(
      action_context_, *ip_address_port_protocol_it_, *adapter);

  address_subscriptions_.Push(
      ip_address_channel_connection_->ResultEvent().Subscribe([this](
                                                                  auto const&) {
        transport_ = ip_address_channel_connection_->transport();
        connection_info_ = ip_address_channel_connection_->connection_info();
        state_.Set(State::Connected);
      }),
      ip_address_channel_connection_->ErrorEvent().Subscribe(
          [this](auto const&) {
            ++ip_address_port_protocol_it_;
            state_.Set(State::TryConnection);
          }));
}

}  // namespace ae
#endif
