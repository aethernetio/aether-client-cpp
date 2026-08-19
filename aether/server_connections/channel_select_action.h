/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_SERVER_CONNECTIONS_CHANNEL_SELECT_ACTION_H_
#define AETHER_SERVER_CONNECTIONS_CHANNEL_SELECT_ACTION_H_

#include <memory>
#include <variant>

#include "aether-miscpp/types/result.h"

#include "aether/actions/action.h"
#include "aether/ae_context.h"
#include "aether/channels/channel.h"
#include "aether/clock.h"
#include "aether/events/events.h"
#include "aether/executors/executors.h"
#include "aether/stream_api/istream.h"

namespace ae {
struct ChannelEntry;

class ChannelSelectAction final : public Action {
  struct CbHandle {
    void operator()(std::optional<Result<std::unique_ptr<ByteIStream>,
                                         std::variant<int, ex::TimeoutError>>>&&
                        res) const noexcept;

    static int HandleError(ex::TimeoutError const& te) noexcept;
    static int HandleError(int e) noexcept;

    ChannelSelectAction* self;
    TimePoint start_time;
  };

 public:
  using ResultEvent = Event<void(Result<std::unique_ptr<ByteIStream>, int>)>;

  ChannelSelectAction(AeContext const& ae_context,
                      ChannelEntry& attempted_channel) noexcept;

  void Start();
  ResultEvent::Subscriber result_event() noexcept;
  ChannelEntry& attempted_channel() noexcept;

 private:
  void ChannelSelected();
  void ChannelFailed();

  AeContext ae_context_;
  ChannelEntry* attempted_channel_;
  ResultEvent result_event_;

  std::optional<ex::AsyncWaiter<
      AeContext,
      std::invoke_result_t<decltype(ex::with_timeout), TransportBuildSender,
                           AeContext, Duration>,
      CbHandle>>
      async_waiter_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_CHANNEL_SELECT_ACTION_H_
