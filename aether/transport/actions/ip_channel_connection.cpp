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

#include "aether/transport/actions/ip_channel_connection.h"

#include "aether/address.h"
#include "aether/adapters/adapter.h"

#include "aether/transport/itransport.h"

#include "aether/tele/tele.h"

namespace ae {

IpAddressChannelConnectionAction::IpAddressChannelConnectionAction(
    ActionContext action_context,
    IpAddressPortProtocol const& ip_address_port_protocol, Adapter& adapter)
    : ChannelConnectionAction(action_context),
      ip_address_port_protocol_{ip_address_port_protocol},
      adapter_{adapter},
      state_{State::Start} {
  AE_TELED_DEBUG("Create IpAddressChannelConnectionAction");

  state_changed_subscription_ =
      state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
}

IpAddressChannelConnectionAction::~IpAddressChannelConnectionAction() {
  AE_TELED_DEBUG("Destroy IpAddressChannelConnectionAction");
}

TimePoint IpAddressChannelConnectionAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::Start:
        TryConnect(current_time);
        break;
      case State::Connected:
        Action::Result(*this);
        break;
      case State::Failed:
        Action::Error(*this);
        break;
        // TODO: add connection timeout
      default:
        break;
    }
  }
  return current_time;
}

std::unique_ptr<ITransport> IpAddressChannelConnectionAction::transport() {
  return std::move(transport_);
}

ConnectionInfo IpAddressChannelConnectionAction::connection_info() const {
  return connection_info_;
}

void IpAddressChannelConnectionAction::TryConnect(
    TimePoint /* current_time */) {
  AE_TELED_DEBUG("TryConnection to address {}", ip_address_port_protocol_);
  auto transport_create_action =
      adapter_.CreateTransport(ip_address_port_protocol_);

  subscriptions_.Push(
      transport_create_action->SubscribeOnResult(
          [this](auto& action) { TransportCreated(action.transport()); }),
      transport_create_action->SubscribeOnError([this](auto const&) {
        AE_TELED_ERROR("Transport create failed");
        state_.Set(State::Failed);
      }));
}

void IpAddressChannelConnectionAction::TransportCreated(
    std::unique_ptr<ITransport> transport) {
  transport_ = std::move(transport);

  auto connection_info = transport_->GetConnectionInfo();
  switch (connection_info.connection_state) {
    case ConnectionState::kConnected: {
      connection_info_ = connection_info;
      connection_info_.destination = ip_address_port_protocol_;
      state_.Set(State::Connected);
      return;
    }
    case ConnectionState::kDisconnected:
    case ConnectionState::kUndefined:
      transport_->Connect();
      break;
    case ConnectionState::kConnecting:
      break;
  }

  subscriptions_.Push(transport_->ConnectionSuccess()
                          .Subscribe([this]() mutable {
                            connection_info_ = transport_->GetConnectionInfo();
                            connection_info_.destination =
                                ip_address_port_protocol_;

                            state_.Set(State::Connected);
                          })
                          .Once(),
                      transport_->ConnectionError()
                          .Subscribe([this]() { state_.Set(State::Failed); })
                          .Once());
}
}  // namespace ae
