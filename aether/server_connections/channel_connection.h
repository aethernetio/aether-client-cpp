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

#ifndef AETHER_SERVER_CONNECTIONS_CHANNEL_CONNECTION_H_
#define AETHER_SERVER_CONNECTIONS_CHANNEL_CONNECTION_H_

#include "aether/common.h"
#include "aether/ptr/ptr.h"
#include "aether/ae_context.h"
#include "aether/channels/channel.h"
#include "aether/stream_api/istream.h"
#include "aether/executors/executors.h"
#include "aether-miscpp/types/small_function.h"

namespace ae {
class Channel;
class ChannelConnection {
 public:
  using ConnectionStateCb =
      SmallFunction<void(Result<ByteIStream&, int> result)>;

  explicit ChannelConnection(AeContext const& ae_context);

  AE_CLASS_NO_COPY_MOVE(ChannelConnection)

  void BuildTransport(Ptr<Channel> const& channel,
                      ConnectionStateCb&& connection_state_cb);
  ByteIStream* stream() const;

 private:
  void UpdateTransportBuildTime(PtrView<Channel> const& c);

  AeContext ae_context_;

  std::optional<
      ex::AnyWaiter<ex::set_value_t(std::unique_ptr<ByteIStream>),
                    ex::set_error_t(int), ex::set_error_t(ex::TimeoutError)>>
      transport_waiter_;
  TimePoint transport_build_start_;
  std::unique_ptr<ByteIStream> transport_stream_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_CHANNEL_CONNECTION_H_
