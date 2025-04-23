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

#include "aether/transport/server/server_channel_stream.h"

#include "aether/aether.h"
#include "aether/actions/action_context.h"

#include "aether/transport/actions/ip_channel_connection.h"
#include "aether/transport/actions/name_address_channel_connection.h"

#include "aether/tele/tele.h"

namespace ae {
namespace _internal {
#if AE_SUPPORT_CLOUD_DNS
inline std::unique_ptr<ChannelConnectionAction> MakeConnectionAction(
    ActionContext action_context, Ptr<Aether> const& aether,
    Ptr<Adapter> const& adapter, NameAddress const& name_address) {
  return make_unique<NameAddressChannelConnectionAction>(
      action_context, name_address, aether, adapter);
}
#endif

inline std::unique_ptr<ChannelConnectionAction> MakeConnectionAction(
    ActionContext action_context, Ptr<Aether> const& /* aether */,
    Ptr<Adapter> const& adapter, IpAddressPortProtocol const& ip_addr) {
  return make_unique<IpAddressChannelConnectionAction>(action_context, ip_addr,
                                                       *adapter);
}
}  // namespace _internal

// TODO: add config
static constexpr auto kBufferGateCapacity = std::size_t{100};

ServerChannelStream::ServerChannelStream(ObjPtr<Aether> const& aether,
                                         Adapter::ptr const& adapter,
                                         Server::ptr const& server,
                                         Channel::ptr const& channel)
    : action_context_{*aether->action_processor},
      server_{server},
      channel_{channel},
      buffer_gate_{action_context_, kBufferGateCapacity},
      connection_action_{std::visit(
          [&](auto const& address) {
            return _internal::MakeConnectionAction(
                ActionContext{*aether->action_processor}, aether, adapter,
                address);
          },
          channel->address)},
      connection_start_time_{Now()},
      connection_timer_{std::in_place, ActionContext{*aether->action_processor},
                        channel->expected_connection_time()} {
  connection_success_ = connection_action_->ResultEvent().Subscribe(
      [this](auto& action) { OnConnected(action); }),
  connection_failed_ = connection_action_->ErrorEvent().Subscribe(
      [this](auto const&) { OnConnectedFailed(); }),
  connection_finished_ = connection_action_->FinishedEvent().Subscribe(
      [this]() { connection_action_.reset(); });

  connection_timeout_ = connection_timer_->ResultEvent().Subscribe(
      [this](auto const&) { OnConnectedFailed(); });
  connection_timer_finished_ = connection_timer_->FinishedEvent().Subscribe(
      [this]() { connection_timer_.reset(); });
}

void ServerChannelStream::OnConnected(ChannelConnectionAction& connection) {
  auto channel_ptr = channel_.Lock();
  assert(channel_ptr);

  auto connection_time =
      std::chrono::duration_cast<Duration>(Now() - connection_start_time_);
  channel_ptr->AddConnectionTime(std::move(connection_time));

  if (!connection_timer_) {
    // probably timeout
    return;
  }
  connection_timer_->Stop();

  transport_ = connection.transport();
  connection_error_ = transport_->ConnectionError().Subscribe(
      *this, MethodPtr<&ServerChannelStream::OnConnectedFailed>{});
  transport_write_gate_.emplace(action_context_, *transport_);
  Tie(buffer_gate_, *transport_write_gate_);
}

void ServerChannelStream::OnConnectedFailed() {
  AE_TELED_ERROR("ServerChannelStream:OnConnectedFailed");
  connection_timeout_.Reset();
  connection_failed_.Reset();
  buffer_gate_.Unlink();
}

}  // namespace ae
