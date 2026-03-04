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

#  include "aether/client.h"
#  include "aether/aether.h"
#  include "aether/uap/uap.h"
#  include "aether/cloud_connections/cloud_visit.h"
#  include "aether/cloud_connections/cloud_server_connection.h"

#  include "aether/tele/tele.h"

namespace ae {
namespace time_sync_internal {
class TimeSyncRequest : public Action<TimeSyncRequest> {
  enum class State : char {
    kEnsureConnected,
    kMakeRequest,
    kWaitResponse,
    kRetry,
    kResult,
    kFailed
  };

  static constexpr auto kRequestTimeout = 10s;
  static constexpr auto kMaxTries = 5;

 public:
  TimeSyncRequest(ActionContext action_context, Ptr<Client> const& client)
      : Action{action_context},
        client_{client},
        state_{State::kEnsureConnected} {
    state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
  }

  UpdateStatus Update() {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kEnsureConnected:
          EnsureConnected();
          break;
        case State::kMakeRequest:
          SyncRequest();
          break;
        case State::kRetry: {
          tries_++;
          if (tries_ >= kMaxTries) {
            state_ = State::kFailed;
          } else {
            state_ = State::kEnsureConnected;
          }
          break;
        }
        case State::kWaitResponse:
          break;
        case State::kResult:
          return UpdateStatus::Result();
        case State::kFailed:
          return UpdateStatus::Error();
      }
    }

    if (state_ == State::kWaitResponse) {
      return WaitResponse();
    }

    return {};
  }

 private:
  void EnsureConnected() {
    auto client_ptr = client_.Lock();
    if (!client_ptr) {
      state_ = State::kFailed;
      return;
    }

    // check or subscribe for connection state to main server
    CloudVisit::Visit(
        [this](CloudServerConnection* sc) {
          assert((sc != nullptr) && "Server connection is null!");

          auto* cc = sc->client_connection();
          assert((cc != nullptr) && "Client connection is null!");

          // if already connected
          if (cc->stream_info().link_state == LinkState::kLinked) {
            state_ = State::kMakeRequest;
            return;
          }

          // wait till connected
          link_state_sub_ = cc->stream_update_event().Subscribe([this, cc]() {
            switch (cc->stream_info().link_state) {
              case LinkState::kLinked:
                state_ = State::kMakeRequest;
                break;
              case LinkState::kLinkError:
                state_ = State::kRetry;
                break;
              default:
                break;
            }
          });
        },
        client_ptr->cloud_connection(), RequestPolicy::MainServer{});
  }

  void SyncRequest() {
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
          assert(((cc != nullptr) &&
                  (cc->stream_info().link_state == LinkState::kLinked)) &&
                 "Client connection is not linked!");

          auto write_action =
              cc->LoginApiCall(SubApi<LoginApi>{[this](auto& api) {
                AE_TELED_DEBUG("Make time sync request");
                ApiPromisePtr<std::uint64_t> promise = api->get_time_utc();
                response_sub_ = promise->StatusEvent().Subscribe(
                    OnResult{[this, request_time{Now()}](auto& p) {
                      HandleResponse(
                          std::chrono::milliseconds{
                              static_cast<std::int64_t>(p.value())},
                          request_time, Now());
                      // time synced
                      state_ = State::kResult;
                    }});
              }});
          write_action_sub_ =
              write_action->StatusEvent().Subscribe(OnError{[this]() {
                AE_TELED_ERROR("Time sync write error, retry");
                state_ = State::kRetry;
              }});
        },
        client_ptr->cloud_connection(), RequestPolicy::MainServer{});

    // use raw time to avoid sync jumps
    request_time_ = Now();
    state_ = State::kWaitResponse;
  }

  UpdateStatus WaitResponse() {
    auto current_time = Now();
    auto timeout = request_time_ + kRequestTimeout;
    if (current_time > timeout) {
      AE_TELED_ERROR("Time sync response timeout");
      state_ = State::kFailed;
      return {};
    }
    return UpdateStatus::Delay(timeout);
  }

  static void HandleResponse(std::chrono::milliseconds server_epoch,
                             TimePoint request_time, TimePoint response_time) {
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
        std::chrono::duration_cast<std::chrono::milliseconds>(diff_time)
            .count());
    // update diff time
    SyncClock::SyncTimeDiff +=
        std::chrono::duration_cast<decltype(SyncClock::SyncTimeDiff)>(
            diff_time);
    AE_TELED_DEBUG("Current time {:%Y-%m-%d %H:%M:%S}", Now());
  }

  PtrView<Client> client_;
  StateMachine<State> state_;
  TimePoint request_time_;
  int tries_{};
  Subscription link_state_sub_;
  Subscription response_sub_;
  Subscription write_action_sub_;
};
}  // namespace time_sync_internal

// set end of time - this means last_sync_time is not set
TimePoint TimeSyncAction::last_sync_time = TimePoint::max();

TimeSyncAction::TimeSyncAction(ActionContext action_context,
                               Ptr<Aether> const& aether,
                               Ptr<Client> const& client,
                               Duration sync_interval)
    : Action{action_context},
      action_context_{action_context},
      aether_{aether},
      client_{client},
      sync_interval_{sync_interval},
      state_{State::kWaitInterval} {
  state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
  AE_TELED_INFO("Time sync created");
}

UpdateStatus TimeSyncAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kMakeRequest:
        MakeRequest();
        break;
      case State::kWaitInterval:
        break;
      case State::kFailed:
        return UpdateStatus::Error();
    }
  }
  if (state_ == State::kWaitInterval) {
    return WaitInterval();
  }
  return {};
}

void TimeSyncAction::MakeRequest() {
  auto aether_ptr = aether_.Lock();
  assert(aether_ptr);

  auto uap = aether_ptr->uap.Load();
  if (!uap) {
    state_ = State::kFailed;
    return;
  }

  // send request only if SendReceive Uap interval enabled
  if (auto timer = uap->timer();
      timer.has_value() &&
      timer->interval().interval.type != IntervalType::kSendReceive) {
    state_ = State::kWaitInterval;
    return;
  }

  auto client_ptr = client_.Lock();
  if (!client_ptr) {
    state_ = State::kFailed;
    return;
  }

  time_sync_request_ = ActionPtr<time_sync_internal::TimeSyncRequest>{
      action_context_, client_ptr};
  uap->RegisterAction(*time_sync_request_);

  // use raw time to avoid sync jumps
  last_sync_time = Now();
  state_ = State::kWaitInterval;
}

UpdateStatus TimeSyncAction::WaitInterval() {
  // the end of time!
  if (last_sync_time == TimePoint::max()) {
    state_ = State::kMakeRequest;
    return {};
  }
  auto current_time = Now();
  auto timeout = last_sync_time + sync_interval_;
  if (current_time > timeout) {
    AE_TELED_INFO("Time sync interval timeout, make new request");
    state_ = State::kMakeRequest;
    return {};
  }
  return UpdateStatus::Delay(timeout);
}

}  // namespace ae
#endif
