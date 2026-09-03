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

#include "aether/ae_actions/ping.h"
#if AE_ENABLE_PING

#  include <cassert>
#  include <cstdint>
#  include <limits>
#  include <utility>

#  include "aether/server.h"

#  include "aether/cloud_connections/cloud_server_connection.h"
#  include "aether/work_cloud_api/work_server_api/authorized_api.h"

#  include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
namespace {

constexpr int kPingPromiseEvictedError{-1};

constexpr int PromiseErrorCodeToPingErrorCode(
    std::uint32_t error_code) noexcept {
  if (error_code == std::numeric_limits<std::uint32_t>::max()) {
    return kPingPromiseEvictedError;
  }
  if (error_code <=
      static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    return static_cast<int>(error_code);
  }
  return kPingPromiseEvictedError;
}

static_assert(PromiseErrorCodeToPingErrorCode(
                  std::numeric_limits<std::uint32_t>::max()) ==
              kPingPromiseEvictedError);
static_assert(PromiseErrorCodeToPingErrorCode(0) == 0);
static_assert(PromiseErrorCodeToPingErrorCode(static_cast<std::uint32_t>(
                  std::numeric_limits<int>::max())) ==
              std::numeric_limits<int>::max());
static_assert(PromiseErrorCodeToPingErrorCode(
                  static_cast<std::uint32_t>(std::numeric_limits<int>::max()) +
                  1U) == kPingPromiseEvictedError);

}  // namespace

Ping::Ping(AeContext const& ae_context,
           CloudServerConnection& cloud_server_connection,
           Duration next_ping_hint, Duration rx_window, Duration timeout)
    : ae_context_{ae_context},
      cloud_server_connection_{&cloud_server_connection},
      next_ping_hint_{next_ping_hint},
      rx_window_{rx_window},
      timeout_{timeout},
      server_id_{cloud_server_connection_->server_id()} {
  // Hard floor and ceiling: zero hard_wait strands presence; multi-minute
  // selected_rtt*8 hard_wait hides cloud ApiPromise failures on browser WSS.
  constexpr auto kMinTimeout =
      std::chrono::milliseconds{AE_DEFAULT_RESPONSE_TIMEOUT_MS};
  constexpr auto kMaxTimeout = std::chrono::milliseconds{60000};
  if (timeout_ < kMinTimeout) {
    timeout_ = kMinTimeout;
  }
  if (timeout_ > kMaxTimeout) {
    timeout_ = kMaxTimeout;
  }
  if (next_ping_hint_ <= Duration{}) {
    next_ping_hint_ = kMinTimeout;
  }
  if (rx_window_ < std::chrono::milliseconds{1}) {
    rx_window_ = next_ping_hint_;
  }
  AE_TELE_INFO(
      kPing,
      "Ping action created to server id: {}, interval_us: {}, rx_window_us: "
      "{}, timeout_us: {}",
      server_id_, next_ping_hint_.count(), rx_window_.count(),
      timeout_.count());
}

Ping::ResultEvent::Subscriber Ping::result_event() { return result_event_; }

void Ping::Start(TimePoint current_time) {
  auto* cc = cloud_server_connection_->client_connection();
  assert(cc != nullptr && "Ping::Start requires a client connection");
  assert(cc->stream_info().link_state == LinkState::kLinked &&
         "Ping::Start requires linked connection");
  assert(state_ == RequestState::kCreated &&
         "Ping::Start must be called only once");
  if (state_ != RequestState::kCreated) {
    return;
  }
  state_ = RequestState::kPending;

  auto& write_action = cc->AuthorizedApiCall(
      SubApi{[this, current_time](ApiContext<AuthorizedApi>& auth_api) {
        // Single nested command only: AuthorizedApi.ping opens the RX window
        // on the server. Do not prepend method 36 (openReceiveWindow) or any
        // other AuthorizedApi call — a former void pull_messages at id 36
        // desynchronized the LoginStream against production openReceiveWindow.
        auto next_ping_hint_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                next_ping_hint_)
                .count());
        auto rx_window_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(rx_window_)
                .count());

        auto pong_promise = auth_api->ping(next_ping_hint_ms, rx_window_ms);
        auto req_id = pong_promise.request_id();

        AE_TELE_DEBUG(kPingSend,
                      "Ping server id {}, request {} expected time {} us",
                      server_id_, req_id, timeout_.count());

        request_start_ = current_time;

        auto wait_result_sub =
            pong_promise.Subscribe([this, req_id](auto&& res) {
              if (res) {
                PingResponse(req_id);
              } else {
                PingResponseError(req_id, res.error());
              }
            });
        wait_result_sub_ = std::move(wait_result_sub);

        // Use std::chrono::milliseconds explicitly for the browser scheduler.
        auto const timeout_ms = std::chrono::milliseconds{
            std::chrono::duration_cast<std::chrono::milliseconds>(timeout_)
                .count()};
        AE_TELED_DEBUG("Ping schedule timeout server {} req {} ms {}",
                       server_id_, req_id, timeout_ms.count());
        timeout_sub_ = ae_context_.scheduler().DelayedTask(
            [this, req_id]() {
              AE_TELED_DEBUG("Ping DelayedTask fired server {} req {}",
                             server_id_, req_id);
              PingResponseTimeout(req_id);
            },
            timeout_ms);
        if (state_ == RequestState::kPending && !timeout_sub_) {
          AE_TELE_ERROR(
              kPingTimeoutError,
              "Ping timeout task allocation failed server id {} request {}",
              server_id_, req_id);
          state_ = RequestState::kFinished;
          ResetRequestSubscriptions();
          result_event_.Emit(PingResult{Error{5}});
        }
      }});

  write_sub_ = write_action.status_event().Subscribe([this](auto status) {
    if (status == WriteAction::Status::kSuccess) {
      AE_TELED_DEBUG("Ping write success server id {}", server_id_);
      return;
    }
    if (status == WriteAction::Status::kFail) {
      if (state_ != RequestState::kPending) {
        return;
      }
      AE_TELE_ERROR(kPingWriteError, "Ping write error");
      state_ = RequestState::kFinished;
      ResetRequestSubscriptions();
      result_event_.Emit(PingResult{Error{1}});
    }
  });
  if (state_ != RequestState::kPending) {
    write_sub_.Reset();
  }
}

void Ping::PingResponse(RequestId request_id) {
  auto current_time = Now();
  auto ping_duration =
      std::chrono::duration_cast<Duration>(current_time - request_start_);

  AE_TELED_DEBUG("Ping received server id {} request {} duration {}",
                 server_id_, request_id, ping_duration);
  if (state_ == RequestState::kPending) {
    state_ = RequestState::kFinished;
    ResetRequestSubscriptions();
    result_event_.Emit(PingResult{Ok{ping_duration}});
  } else if (state_ == RequestState::kTimedOut) {
    wait_result_sub_.Reset();
    state_ = RequestState::kFinished;
    result_event_.Emit(PingResult{LateDuration{ping_duration}});
  }
}

void Ping::PingResponseError(RequestId request_id, std::int32_t error_code) {
  AE_TELE_ERROR(kPingResponseError,
                "Ping error server id {} request {} code {}", server_id_,
                request_id, error_code);
  if (state_ == RequestState::kPending) {
    state_ = RequestState::kFinished;
    ResetRequestSubscriptions();
    if (error_code == -1) {
      result_event_.Emit(PingResult{Error{4}});
    } else {
      result_event_.Emit(PingResult{Error{3}});
    }
  } else if (state_ == RequestState::kTimedOut) {
    wait_result_sub_.Reset();
    state_ = RequestState::kFinished;
  }
}

void Ping::PingResponseTimeout(RequestId request_id) {
  if (state_ != RequestState::kPending) {
    return;
  }
  AE_TELE_ERROR(kPingTimeoutError, "Ping timeout server id {} request {}",
                server_id_, request_id);
  state_ = RequestState::kTimedOut;
  timeout_sub_.Reset();
  write_sub_.Reset();
  result_event_.Emit(PingResult{Error{2}});
}

void Ping::ResetRequestSubscriptions() {
  wait_result_sub_.Reset();
  timeout_sub_.Reset();
  write_sub_.Reset();
}

}  // namespace ae
#endif  // AE_ENABLE_PING
