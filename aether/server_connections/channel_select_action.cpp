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

#include "aether/server_connections/channel_select_action.h"

#include "aether/channels/channel.h"
#include "aether/ptr/ptr_view.h"
#include "aether/server_connections/server_connection.h"

#include "aether/tele.h"

namespace ae {
void ChannelSelectAction::CbHandle::operator()(
    std::optional<Result<std::unique_ptr<ByteIStream>,
                         std::variant<int, ex::TimeoutError>>>&& res)
    const noexcept {
  auto channel = self->attempted_channel_->channel.Lock();
  assert(channel && "Channel is null");

  if (res && res->IsOk()) {
    // update transport build time on success
    auto build_time = std::chrono::duration_cast<Duration>(Now() - start_time);
    AE_TELED_INFO("Transport built for {:%S}", build_time);
    channel->channel_statistics().AddConnectionTime(build_time);
  }

  // emit the res, but convert all errors to int
  constexpr int kStopped = -2;
  self->result_event_.Emit(
      std::move(res)
          .value_or(Error<std::variant<int, ex::TimeoutError>>(kStopped))
          .Else([&](auto&& verr) noexcept
                    -> Result<std::unique_ptr<ByteIStream>, int> {
            return std::visit(
                [&](auto const& e) noexcept { return Error{HandleError(e)}; },
                std::forward<decltype(verr)>(verr));
          }));
  self->Finish();
}

int ChannelSelectAction::CbHandle::HandleError(
    ex::TimeoutError const&) noexcept {
  AE_TELED_ERROR("Transport build timeout");
  return -1;
}

int ChannelSelectAction::CbHandle::HandleError(int e) noexcept {
  AE_TELED_ERROR("Transport build failed with error code: {}", e);
  return e;
}

ChannelSelectAction::ChannelSelectAction(
    AeContext const& ae_context, ChannelEntry& attempted_channel) noexcept
    : ae_context_{ae_context}, attempted_channel_{&attempted_channel} {}

void ChannelSelectAction::Start() {
  auto channel = attempted_channel_->channel.Lock();
  assert(channel && "Channel is null");

  auto s = channel->TransportBuilder() |
           ex::with_timeout(ae_context_, channel->TransportBuildTimeout());

  async_waiter_.emplace(ae_context_, std::move(s),
                        CbHandle{.self = this, .start_time = Now()});
}

auto ChannelSelectAction::result_event() noexcept -> ResultEvent::Subscriber {
  return EventSubscriber{result_event_};
}

ChannelEntry& ChannelSelectAction::attempted_channel() noexcept {
  assert(attempted_channel_ != nullptr);
  return *attempted_channel_;
}
}  // namespace ae
