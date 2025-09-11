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

#include "aether/channel.h"

#include <chrono>

#include "aether/config.h"

namespace ae {

Channel::Channel(Adapter::ptr adapter, Domain* domain)
    : Obj{domain},
      adapter_{std::move(adapter)},
      channel_statistics_{domain->CreateObj<ChannelStatistics>()} {
  channel_statistics_->AddPingTime(
      std::chrono::milliseconds{AE_DEFAULT_PING_TIMEOUT_MS});
  channel_statistics_->AddConnectionTime(
      std::chrono::milliseconds{AE_DEFAULT_CONNECTION_TIMEOUT_MS});
}

ActionPtr<TransportBuilderAction> Channel::TransportBuilder() {
  if (!adapter_) {
    domain_->LoadRoot(adapter_);
  }
  return adapter_->CreateTransport(address);
}

void Channel::AddConnectionTime(Duration connection_time) {
  channel_statistics_->AddConnectionTime(std::move(connection_time));
}

void Channel::AddPingTime(Duration ping_time) {
  channel_statistics_->AddPingTime(std::move(ping_time));
}

Duration Channel::expected_connection_time() const {
  assert(!channel_statistics_->connection_time_statistics().empty());
  return 2 * channel_statistics_->connection_time_statistics().percentile<99>();
}

Duration Channel::expected_ping_time() const {
  assert(!channel_statistics_->ping_time_statistics().empty());
  return channel_statistics_->ping_time_statistics().percentile<99>();
}

std::size_t Channel::max_packet_size() const {
  // TODO: make it depend on the adapter's capabilities
  auto protocol =
      std::visit([](auto const& addr) { return addr.protocol; }, address);
  switch (protocol) {
    case Protocol::kTcp:
      return static_cast<std::size_t>(
          std::numeric_limits<std::uint32_t>::max());
    case Protocol::kUdp:
      return 1200;
    default:
      break;
  }
  return 0;
}

}  // namespace ae
