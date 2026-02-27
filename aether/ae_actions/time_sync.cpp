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

#  include "aether/cloud_connections/cloud_visit.h"
#  include "aether/cloud_connections/cloud_server_connection.h"

#  include "aether/tele/tele.h"

namespace ae {
SystemTimePoint TimeSyncAction::last_sync_time = SystemTimePoint::max();

TimeSyncAction::TimeSyncAction(ActionContext action_context,
                               Ptr<Client> const& client,
                               Duration sync_interval)
    : Action{action_context},
      client_{client},
      sync_interval_{sync_interval},
      state_{State::kWaitInterval} {
  state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
  AE_TELED_INFO("Time sync created");
}

UpdateStatus TimeSyncAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kEnsureConnected:
        EnsureConnected();
        break;
      case State::kMakeRequest:
        SyncRequest();
        break;
      case State::kWaitResponse:
      case State::kWaitInterval:
        break;
      case State::kFailed:
        return UpdateStatus::Error();
    }
  }
  if (state_ == State::kWaitResponse) {
    return WaitResponse();
  }
  if (state_ == State::kWaitInterval) {
    return WaitInterval();
  }
  return {};
}

void TimeSyncAction::EnsureConnected() {
  auto client_ptr = client_.Lock();
  if (!client_ptr) {
    state_ = State::kFailed;
    return;
  }

  CloudVisit::Visit(
      [this](CloudServerConnection* sc) {
        assert((sc != nullptr) && "Server connection is null!");
        auto is_linked = [sc]() {
          return sc->client_connection()->stream_info().link_state ==
                 LinkState::kLinked;
        };

        if (is_linked()) {
          state_ = State::kMakeRequest;
          return;
        }
        link_state_sub_ =
            sc->client_connection()->stream_update_event().Subscribe(
                [this, is_linked]() {
                  if (is_linked()) {
                    state_ = State::kMakeRequest;
                  }
                });
      },
      client_ptr->cloud_connection(), RequestPolicy::MainServer{});
}

void TimeSyncAction::SyncRequest() {
  auto client_ptr = client_.Lock();
  if (!client_ptr) {
    state_ = State::kFailed;
    return;
  }

  // send get_time_utc request to main server
  // get_time_utc return server utc time point in microseconds
  CloudVisit::Visit(
      [this](CloudServerConnection* sc) {
        assert((sc != nullptr) && "Server connection is null!");

        auto* cc = sc->client_connection();
        assert((cc->stream_info().link_state == LinkState::kLinked) &&
               "Client connection is not linked!");

        write_action_sub_ =
            cc->LoginApiCall(SubApi<LoginApi>{[this](auto& api) {
                AE_TELED_DEBUG("Make time sync request");
                ApiPromisePtr<uint64_t> promise = api->get_time_utc();
                response_sub_ = promise->StatusEvent().Subscribe(
                    OnResult{[this, request_time{Clock::now()}](auto& p) {
                      HandleResponse(
                          std::chrono::milliseconds{
                              static_cast<std::int64_t>(p.value())},
                          request_time, Clock::now());
                      // time synced, wait for next sync interval
                      state_ = State::kWaitInterval;
                    }});
              }})
                ->StatusEvent()
                .Subscribe(OnError{[this]() {
                  AE_TELED_ERROR("Time sync write error, repeat");
                  state_ = State::kEnsureConnected;
                }});
      },
      client_ptr->cloud_connection(), RequestPolicy::MainServer{});

  // use raw time to avoid sync jumps
  last_sync_time = SystemTime();
  state_ = State::kWaitResponse;
}

void TimeSyncAction::HandleResponse(std::chrono::milliseconds server_epoch,
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
  Clock::SyncTimeDiff +=
      std::chrono::duration_cast<decltype(Clock::SyncTimeDiff)>(diff_time);
  AE_TELED_DEBUG("Current time {:%Y-%m-%d %H:%M:%S}", Now());
}

UpdateStatus TimeSyncAction::WaitResponse() {
  auto current_time = SystemTime();
  auto timeout = last_sync_time + kRequestTimeout;
  if (current_time > timeout) {
    AE_TELED_ERROR("Time sync response timeout, make new request");
    state_ = State::kEnsureConnected;
    return {};
  }
  return UpdateStatus::Delay(ToSyncTime(timeout));
}

UpdateStatus TimeSyncAction::WaitInterval() {
  // the end of time!
  if (last_sync_time == SystemTimePoint::max()) {
    state_ = State::kEnsureConnected;
    return {};
  }
  auto current_time = SystemTime();
  auto timeout = last_sync_time + sync_interval_;
  if (current_time > timeout) {
    AE_TELED_INFO("Time sync interval timeout, make new request");
    state_ = State::kEnsureConnected;
    return {};
  }
  return UpdateStatus::Delay(ToSyncTime(timeout));
}

}  // namespace ae
#endif
