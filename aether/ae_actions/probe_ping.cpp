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

#include "aether/ae_actions/probe_ping.h"
#if AE_ENABLE_PING

#  include <cassert>
#  include <chrono>
#  include <utility>

#  include "aether/cloud_connections/cloud_server_connection.h"
#  include "aether/work_cloud_api/work_server_api/authorized_api.h"

namespace ae {

ProbePing::ProbePing(AeContext const& ae_context,
                     CloudServerConnection& cloud_server_connection,
                     std::uint64_t probe_id, Duration current_rx_window,
                     Duration next_rx_delay, Duration next_rx_window,
                     Duration timeout)
    : ae_context_{ae_context},
      cloud_server_connection_{&cloud_server_connection},
      probe_id_{probe_id},
      current_rx_window_{current_rx_window},
      next_rx_delay_{next_rx_delay},
      next_rx_window_{next_rx_window},
      timeout_{timeout},
      server_id_{cloud_server_connection_->server_id()} {}

ProbePing::ResultEvent::Subscriber ProbePing::result_event() {
  return result_event_;
}

void ProbePing::Start(TimePoint current_time) {
  auto* cc = cloud_server_connection_->client_connection();
  assert(cc != nullptr);
  assert(state_ == RequestState::kCreated);
  if (state_ != RequestState::kCreated) {
    return;
  }
  state_ = RequestState::kPending;

  auto& write_action = cc->AuthorizedApiCall(SubApi{[this, current_time](
                                                        ApiContext<AuthorizedApi>&
                                                            auth_api) {
    auto cur_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            current_rx_window_)
            .count());
    auto delay_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(next_rx_delay_)
            .count());
    auto next_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(next_rx_window_)
            .count());

    auto promise =
        auth_api->probe_ping(probe_id_, cur_ms, delay_ms, next_ms);
    auto req_id = promise.request_id();
    request_start_ = current_time;
    wait_result_sub_ = promise.Subscribe([this, req_id](auto&& res) {
      if (res) {
        OnOk(req_id);
      } else {
        OnError(req_id, res.error());
      }
    });
    timeout_sub_ = ae_context_.scheduler().DelayedTask(
        [this, req_id]() { OnTimeout(req_id); }, current_time + timeout_);
  }});

  write_sub_ = write_action.status_event().Subscribe([this](auto status) {
    if (status == WriteAction::Status::kFail &&
        state_ == RequestState::kPending) {
      state_ = RequestState::kFinished;
      ResetSubs();
      result_event_.Emit(ProbePingResult{Error{1}});
    }
  });
}

void ProbePing::OnOk(RequestId) {
  auto const dur =
      std::chrono::duration_cast<Duration>(Now() - request_start_);
  if (state_ == RequestState::kPending) {
    state_ = RequestState::kFinished;
    ResetSubs();
    result_event_.Emit(ProbePingResult{Ok{dur}});
  } else if (state_ == RequestState::kTimedOut) {
    wait_result_sub_.Reset();
    state_ = RequestState::kFinished;
    result_event_.Emit(ProbePingResult{LateDuration{dur}});
  }
}

void ProbePing::OnError(RequestId, std::int32_t) {
  if (state_ == RequestState::kPending) {
    state_ = RequestState::kFinished;
    ResetSubs();
    result_event_.Emit(ProbePingResult{Error{3}});
  } else if (state_ == RequestState::kTimedOut) {
    wait_result_sub_.Reset();
    state_ = RequestState::kFinished;
  }
}

void ProbePing::OnTimeout(RequestId) {
  if (state_ != RequestState::kPending) {
    return;
  }
  state_ = RequestState::kTimedOut;
  timeout_sub_.Reset();
  write_sub_.Reset();
  result_event_.Emit(ProbePingResult{Error{2}});
}

void ProbePing::ResetSubs() {
  wait_result_sub_.Reset();
  timeout_sub_.Reset();
  write_sub_.Reset();
}

}  // namespace ae
#endif  // AE_ENABLE_PING
