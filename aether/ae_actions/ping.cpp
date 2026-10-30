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

#  include "aether/cloud_connections/cloud_server_connection.h"
#  include "aether/diagnostics/bench_connection_event_diag.h"
#  include "aether/server_connections/client_server_connection.h"
#  include "aether/work_cloud_api/work_server_api/authorized_api.h"
#  include "aether/write_action/write_action.h"

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

#  if AE_BENCH_CONNECTION_EVENT_DIAG
int WriteStatusToInt(WriteAction::Status status) {
  switch (status) {
    case WriteAction::Status::kSuccess:
      return 0;
    case WriteAction::Status::kFail:
      return 1;
    case WriteAction::Status::kStop:
      return 2;
  }
  return -1;
}

void RecordPingDiag(CloudServerConnection* cloud, char const* event,
                    RequestId request_id, std::uint32_t original_timeout_ms,
                    std::uint32_t applied_timeout_ms, int write_status,
                    int error_code) {
  if (cloud == nullptr) {
    return;
  }
  std::uint64_t const cloud_id = cloud->bench_cloud_instance_id();
  std::uint64_t client_id = 0;
  if (auto* cc = cloud->client_connection()) {
    client_id = cc->bench_client_instance_id();
  }
  AE_BENCH_CONN_DIAG(event, cloud->server_id(), cloud_id, client_id, request_id,
                     original_timeout_ms, applied_timeout_ms, write_status,
                     error_code, -1);
}
#  endif

}  // namespace

Ping::Ping(AeContext const& ae_context,
           CloudServerConnection& cloud_server_connection,
           Duration next_ping_hint, Duration rx_window,
           Duration response_timeout, Duration channel_response_timeout)
    : ae_context_{ae_context},
      cloud_server_connection_{&cloud_server_connection},
      next_ping_hint_{next_ping_hint},
      rx_window_{rx_window},
      timeout_{response_timeout},
      server_id_{cloud_server_connection_->server_id()},
      result_event_{ae_context_} {
#if AE_BENCH_CONNECTION_EVENT_DIAG
  diag_applied_timeout_ms_ = static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(response_timeout)
          .count());
  diag_original_timeout_ms_ = static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          channel_response_timeout)
          .count());
#endif
  AE_TELE_INFO(
      kPing,
      "Ping action created to server id: {}, interval: {:%S}s, rx_window: "
      "{:%S}s, timeout: {:%S}s",
      server_id_, next_ping_hint_, rx_window_, timeout_);
}

Ping::ResultEvent const& Ping::result_event() const { return result_event_; }

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
#  if AE_BENCH_CONNECTION_EVENT_DIAG
  diag_write_terminal_ = false;
  diag_active_request_id_ = 0;
  RecordPingDiag(cloud_server_connection_, "ping_start", 0,
                 diag_original_timeout_ms_, diag_applied_timeout_ms_, -1, 0);
#  endif

  auto& write_action = cc->AuthorizedApiCall(
      [this, current_time](ApiContext<AuthorizedApi>& auth_api) {
        auto next_ping_hint_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                next_ping_hint_)
                .count());
        auto rx_window_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(rx_window_)
                .count());

        auto pong_promise = auth_api->Ping(next_ping_hint_ms, rx_window_ms);
        auto req_id = pong_promise.request_id();

        AE_TELE_DEBUG(kPingSend,
                      "Ping server id {}, request {} expected time {}",
                      server_id_, req_id, timeout_);

        request_start_ = current_time;
#  if AE_BENCH_CONNECTION_EVENT_DIAG
        diag_active_request_id_ = req_id;
        RecordPingDiag(cloud_server_connection_, "ping_request", req_id,
                       diag_original_timeout_ms_, diag_applied_timeout_ms_, -1,
                       0);
#  endif

        auto wait_result_sub =
            pong_promise.Subscribe([this, req_id](auto&& res) {
              if (res) {
                PingResponse(req_id);
              } else {
                PingResponseError(req_id, res.error());
              }
            });
        wait_result_sub_ = std::move(wait_result_sub);

        timeout_sub_ = ae_context_.scheduler().DelayedTask(
            [this, req_id]() { PingResponseTimeout(req_id); },
            current_time + timeout_);
        if (state_ == RequestState::kPending && !timeout_sub_) {
          AE_TELE_ERROR(
              kPingTimeoutError,
              "Ping timeout task allocation failed server id {} request {}",
              server_id_, req_id);
          state_ = RequestState::kFinished;
          ResetRequestSubscriptions();
          result_event_.Emit(PingResult{Error{5}});
        }
      });

  write_sub_ = write_action.status_event().Subscribe([this](auto status) {
#  if AE_BENCH_CONNECTION_EVENT_DIAG
    if (!diag_write_terminal_) {
      diag_write_terminal_ = true;
      char const* ev = "ping_write_success";
      if (status == WriteAction::Status::kFail) {
        ev = "ping_write_fail";
      } else if (status == WriteAction::Status::kStop) {
        ev = "ping_write_stop";
      }
      RecordPingDiag(cloud_server_connection_, ev, diag_active_request_id_,
                     diag_original_timeout_ms_, diag_applied_timeout_ms_,
                     WriteStatusToInt(status), 0);
    }
#  endif
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

#  if DEBUG
  cc->LoginApiCall([&](ApiContext<LoginApi>& api_call) {
    api_call->GetMyIp().Subscribe([&](auto&& res) noexcept {
      if (res) {
        auto&& ip = std::forward<decltype(res)>(res).value();
        AE_TELED_DEBUG("Server id: {}, our public ip: {}:{}, coords: {},{}",
                       server_id_, ip.ip, ip.port, ip.latitude, ip.longitude);
      } else {
        AE_TELED_ERROR("Get my ip request error {}",
                       std::forward<decltype(res)>(res).error());
      }
    });
  });
#  endif
}

void Ping::PingResponse(RequestId request_id) {
  auto current_time = Now();
  auto ping_duration =
      std::chrono::duration_cast<Duration>(current_time - request_start_);

  AE_TELED_DEBUG("Ping received server id {} request {} duration {}",
                 server_id_, request_id, ping_duration);
  if (state_ == RequestState::kPending) {
#  if AE_BENCH_CONNECTION_EVENT_DIAG
    RecordPingDiag(cloud_server_connection_, "ping_pong", request_id,
                   diag_original_timeout_ms_, diag_applied_timeout_ms_, -1, 0);
#  endif
    state_ = RequestState::kFinished;
    ResetRequestSubscriptions();
    result_event_.Emit(PingResult{Ok{ping_duration}});
  } else if (state_ == RequestState::kTimedOut) {
#  if AE_BENCH_CONNECTION_EVENT_DIAG
    RecordPingDiag(cloud_server_connection_, "ping_pong_late", request_id,
                   diag_original_timeout_ms_, diag_applied_timeout_ms_, -1, 0);
#  endif
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
#  if AE_BENCH_CONNECTION_EVENT_DIAG
    RecordPingDiag(cloud_server_connection_, "ping_error", request_id,
                   diag_original_timeout_ms_, diag_applied_timeout_ms_, -1,
                   static_cast<int>(error_code));
#  endif
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
#  if AE_BENCH_CONNECTION_EVENT_DIAG
  RecordPingDiag(cloud_server_connection_, "ping_timeout", request_id,
                 diag_original_timeout_ms_, diag_applied_timeout_ms_, -1, 0);
#  endif
  state_ = RequestState::kTimedOut;
#  if AE_BENCH_CONNECTION_EVENT_DIAG
  if (!diag_write_terminal_ && diag_active_request_id_.id != 0) {
    RecordPingDiag(cloud_server_connection_, "ping_write_unobserved",
                   diag_active_request_id_, diag_original_timeout_ms_,
                   diag_applied_timeout_ms_, -1, 0);
  }
#  endif
  timeout_sub_.Reset();
  write_sub_.Reset();
  result_event_.Emit(PingResult{Error{2}});
}

void Ping::ResetRequestSubscriptions() {
#  if AE_BENCH_CONNECTION_EVENT_DIAG
  if (!diag_write_terminal_ && diag_active_request_id_.id != 0) {
    RecordPingDiag(cloud_server_connection_, "ping_write_unobserved",
                   diag_active_request_id_, diag_original_timeout_ms_,
                   diag_applied_timeout_ms_, -1, 0);
  }
#  endif
  wait_result_sub_.Reset();
  timeout_sub_.Reset();
  write_sub_.Reset();
}

}  // namespace ae
#endif  // AE_ENABLE_PING
