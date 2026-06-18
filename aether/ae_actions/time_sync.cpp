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

#include "aether/ae_actions/time_sync.h"

#if AE_TIME_SYNC_ENABLED

#  include <cassert>

#  include "aether/client.h"
#  include "aether/aether.h"
#  include "aether/uap/uap.h"
#  include "aether-miscpp/misc/override.h"
#  include "aether/types/iterator.h"
#  include "aether/executors/executors.h"
#  include "aether/cloud_connections/cloud_visit.h"
#  include "aether/cloud_connections/cloud_server_connection.h"

#  include "aether/tele/tele.h"

namespace ae {
namespace time_sync_internal {
static constexpr auto kRequestTimeout = 10s;
static constexpr auto kMaxTries = 5;

auto TimeSyncRequest::EnsureConnected() {
  return ex::create<ex::set_value_t(Success), ex::set_error_t(Failed),
                    ex::set_error_t(Retry)>([&](auto& ctx) noexcept {
    auto client_ptr = client_.Lock();
    if (!client_ptr) {
      return ex::set_error(std::move(ctx.receiver), Failed{});
    }

    // check or subscribe for connection state to main server
    CloudVisit::Visit(
        [&](CloudServerConnection* sc) {
          assert((sc != nullptr) && "Server connection is null!");

          auto* cc = sc->client_connection();
          assert((cc != nullptr) && "Client connection is null!");

          // if already connected
          if (cc->stream_info().link_state == LinkState::kLinked) {
            return ex::set_value(std::move(ctx.receiver), Success{});
          }

          // wait till connected
          link_state_sub_ = cc->stream_update_event().Subscribe([&, cc]() {
            switch (cc->stream_info().link_state) {
              case LinkState::kLinked:
                return ex::set_value(std::move(ctx.receiver), Success{});
                break;
              case LinkState::kLinkError:
                return ex::set_error(std::move(ctx.receiver), Retry{});
                break;
              default:
                break;
            }
          });
        },
        client_ptr->cloud_connection(), RequestPolicy::MainServer{});
  });
}

auto TimeSyncRequest::SyncRequest() {
  return ex::let_value([&](Success) noexcept {
    return ex::create<ex::set_value_t(Success), ex::set_error_t(Failed),
                      ex::set_error_t(Retry)>([&](auto& ctx) noexcept {
      auto client_ptr = client_.Lock();
      if (!client_ptr) {
        return ex::set_error(std::move(ctx.receiver), Failed{});
      }

      // send get_time_utc request to main server
      // get_time_utc return server utc time point in microseconds
      CloudVisit::Visit(
          [&](CloudServerConnection* sc) {
            assert((sc != nullptr) && "Server connection is null!");

            auto* cc = sc->client_connection();
            assert(((cc != nullptr) &&
                    (cc->stream_info().link_state == LinkState::kLinked)) &&
                   "Client connection is not linked!");

            auto& write_action =
                cc->LoginApiCall(SubApi<LoginApi>{[&](auto& api) {
                  AE_TELED_DEBUG("Make time sync request");
                  response_sub_ = api->get_time_utc().Subscribe(
                      [&, request_time{Now()}](auto const& p) {
                        if (!p) {
                          return ex::set_error(std::move(ctx.receiver),
                                               Retry{});
                        }
                        HandleResponse(
                            std::chrono::milliseconds{
                                static_cast<std::int64_t>(p.value())},
                            request_time, Now());
                        // time synced
                        return ex::set_value(std::move(ctx.receiver),
                                             Success{});
                      });
                }});
            write_action_sub_ =
                write_action.status_event().Subscribe([&](auto status) {
                  if (status == WriteAction::Status::kFail) {
                    AE_TELED_ERROR("Time sync write error, retry");
                    return ex::set_error(std::move(ctx.receiver), Retry{});
                  }
                });
          },
          client_ptr->cloud_connection(), RequestPolicy::MainServer{});

      // use raw time to avoid sync jumps
      request_time_ = Now();
    });
  });
}

TimeSyncRequest::TimeSyncRequest(AeContext const& ae_context,
                                 Ptr<Client> const& client)
    : ae_context_{ae_context}, client_{client} {
  auto s =
      ex::for_range(Range{1, kMaxTries},
                    [&](auto) {
                      return EnsureConnected() | SyncRequest() |
                             ex::with_timeout(ae_context_, kRequestTimeout) |
                             ex::let_error(Override{
                                 [](Retry) noexcept {
                                   AE_TELED_ERROR("Time sync retry");
                                   return ex::just(ex::for_continue);
                                 },
                                 [](ex::TimeoutError) noexcept {
                                   AE_TELED_ERROR("Time sync response timeout");
                                   return ex::just(ex::for_continue);
                                 },
                                 [](auto&&...) noexcept {
                                   AE_TELED_ERROR("Time sync failed");
                                   return ex::just_error(Failed{});
                                 },
                             });
                    }) |
      ex::let_stopped([]() noexcept { return ex::just_error(Failed{}); });

  waiter_.emplace(
      ae_context_, std::move(s),
      [&](std::optional<Result<Success, Failed>> const& res) noexcept {
        if (!res || !*res) {
          AE_TELED_ERROR("Time sync failed");
        }
        if (res && *res) {
          AE_TELED_INFO("Time sync succeeded");
        }
        Finish();
      });
}

void TimeSyncRequest::HandleResponse(std::chrono::milliseconds server_epoch,
                                     TimePoint request_time,
                                     TimePoint response_time) {
  auto server_time = TimePoint{server_epoch};
  auto round_trip = response_time - request_time;
  AE_TELED_DEBUG(
      "Time sync roundtrip {:%S} request_time {:%Y-%m-%d %H:%M:%S}, "
      "response_time {:%Y-%m-%d %H:%M:%S} server_time {:%Y-%m-%d %H:%M:%S}",
      std::chrono::duration_cast<Duration>(round_trip), request_time,
      response_time, server_time);

  auto diff_time = server_time - request_time - round_trip / 2;
  AE_TELED_INFO(
      "Time sync diff_time is {} ms",
      std::chrono::duration_cast<std::chrono::milliseconds>(diff_time).count());
  // update diff time
  SyncClock::SyncTimeDiff +=
      std::chrono::duration_cast<decltype(SyncClock::SyncTimeDiff)>(diff_time);
  AE_TELED_DEBUG("Current time {:%Y-%m-%d %H:%M:%S}", SyncClock::now());
}

}  // namespace time_sync_internal

// set end of time - this means last_sync_time is not set
TimePoint TimeSyncAction::last_sync_time = TimePoint::max();

TimeSyncAction::TimeSyncAction(AeContext const& ae_context,
                               Ptr<Client> const& client,
                               Duration sync_interval)
    : ae_context_{ae_context}, client_{client}, sync_interval_{sync_interval} {
  AE_TELED_INFO("Time sync created");
  // the end of time! It means never synced before
  if (last_sync_time == TimePoint::max()) {
    MakeRequest();
  } else {
    ScheduleNextSync();
  }
}

void TimeSyncAction::MakeRequest() {
  auto uap = ae_context_.aether().uap.Load();
  if (!uap) {
    return;
  }

  // send request only if SendReceive Uap interval enabled
  if (auto timer = uap->timer();
      timer.has_value() &&
      timer->interval().interval.type != IntervalType::kSendReceive) {
    ScheduleNextSync();
    return;
  }

  auto client_ptr = client_.Lock();
  if (!client_ptr) {
    return;
  }

  assert((!time_sync_request_ || time_sync_request_->is_finished()) &&
         "Time sync request already in progress");
  time_sync_request_.emplace(ae_context_, client_ptr);
  uap->RegisterStart();
  time_sync_request_->finished_event().Subscribe(
      [uap]() { uap->RegisterEnd(); });

  // use raw time to avoid sync jumps
  last_sync_time = Now();
  ScheduleNextSync();
}

void TimeSyncAction::ScheduleNextSync() {
  TimePoint next_time =
      ((last_sync_time == TimePoint::max()) ? Now() : last_sync_time) +
      sync_interval_;

  task_sub_ = ae_context_.scheduler().DelayedTask([this]() { MakeRequest(); },
                                                  next_time);
}
}  // namespace ae
#endif
