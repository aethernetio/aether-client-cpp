/*
 * Copyright 2025 Aethernet Inc.
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

#include "aether/server_connections/channel_connection.h"

#include <utility>
#include <variant>

#include "aether/misc/override.h"

#include "aether/tele/tele.h"

namespace ae {
ChannelConnection::ChannelConnection(AeContext const& ae_context)
    : ae_context_{ae_context} {}

ByteIStream* ChannelConnection::stream() const {
  return transport_stream_.get();
}

void ChannelConnection::BuildTransport(
    Ptr<Channel> const& channel, ConnectionStateCb&& connection_state_cb) {
  transport_build_start_ = Now();
  auto sender = channel->TransportBuilder();
  transport_waiter_.emplace(
      ae_context_,
      std::move(sender) |
          ex::with_timeout(ae_context_, channel->TransportBuildTimeout()),
      [&, c_ = PtrView<Channel>{channel},
       cb_ = std::move(connection_state_cb)](auto&& result) {
        // the result must exists
        assert(!!result && "The result must exists");
        if (result->IsOk()) {
          UpdateTransportBuildTime(c_);
          transport_stream_ = std::move(result->value());
          assert(transport_stream_ && "Transport should be created");
          cb_(Ok<ByteIStream&>{*transport_stream_});
        } else {
          std::visit(Override{
                         [&](ex::TimeoutError) {
                           AE_TELED_ERROR("Transport build timeout");
                           cb_(Error{-1});
                         },
                         [&](int e) {
                           AE_TELED_ERROR(
                               "Transport build failed with error code: {}", e);
                           cb_(Error{e});
                         },
                     },
                     result->error());
        }
        transport_waiter_.reset();
      });
}

void ChannelConnection::UpdateTransportBuildTime(PtrView<Channel> const& c) {
  auto channel = c.Lock();
  assert(channel && "Channel not loaded");
  auto build_time =
      std::chrono::duration_cast<Duration>(Now() - transport_build_start_);
  AE_TELED_INFO("Transport built for {:%S}", build_time);
  channel->channel_statistics().AddConnectionTime(build_time);
}

}  // namespace ae
