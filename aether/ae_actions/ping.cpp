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

#  include "aether/server.h"

#  include "aether/cloud_connections/cloud_server_connection.h"
#  include "aether/work_cloud_api/work_server_api/authorized_api.h"

#  include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
Ping::Ping(AeContext const& ae_context,
           CloudServerConnection& cloud_server_connection,
           Duration next_ping_hint, Duration rx_window, Duration timeout)
    : ae_context_{ae_context},
      cloud_server_connection_{&cloud_server_connection},
      next_ping_hint_{next_ping_hint},
      rx_window_{rx_window},
      timeout_{timeout},
      server_id_{cloud_server_connection_->server()->server_id} {
  AE_TELE_INFO(
      kPing,
      "Ping action created to server id: {}, interval: {:%S}s, rx_window: "
      "{:%S}s, timeout: {:%S}s",
      server_id_, next_ping_hint_, rx_window_, timeout_);
}

Ping::ResultEvent::Subscriber Ping::result_event() { return result_event_; }

void Ping::Start(TimePoint current_time) {
  auto* cc = cloud_server_connection_->client_connection();
  assert(cc != nullptr && "Ping::Start requires a client connection");
  assert(cc->stream_info().link_state == LinkState::kLinked &&
         "Ping::Start requires linked connection");
  assert(!started_ && "Ping::Start must be called only once");
  if (started_) {
    return;
  }
  started_ = true;

  AE_TELE_DEBUG(kPingSend, "Send ping");

  auto& write_action = cc->AuthorizedApiCall(
      SubApi{[this, current_time](ApiContext<AuthorizedApi>& auth_api) {
        auto next_ping_hint_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                next_ping_hint_)
                .count());
        auto rx_window_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(rx_window_)
                .count());

        auto& pong_promise = auth_api->ping(next_ping_hint_ms, rx_window_ms);
        auto req_id = pong_promise.request_id();

        AE_TELED_DEBUG("Ping server id {}, request {} expected time {:%S}s",
                       server_id_, req_id, timeout_);

        request_start_ = current_time;
        request_id_ = req_id;

        auto wait_result_sub = pong_promise.Subscribe(
            [this, req_id](auto&&...) { PingResponse(req_id); });
        wait_result_sub_ = std::move(wait_result_sub);

        auto timeout_sub = ae_context_.scheduler().DelayedTask(
            [this, req_id]() { PingResponseTimeout(req_id); },
            current_time + timeout_);
        timeout_sub_ = std::move(timeout_sub);
      }});

  write_sub_ = write_action.status_event().Subscribe([this](auto status) {
    if (status == WriteAction::Status::kFail) {
      AE_TELE_ERROR(kPingWriteError, "Ping write error");
      ResetRequestSubscriptions();
      result_event_.Emit(Error{1});
    }
  });

#  if DEBUG
  cc->LoginApiCall(SubApi{[&](ApiContext<LoginApi>& api_call) {
    api_call->get_my_ip().Subscribe([&](auto&& res) noexcept {
      if (res) {
        auto&& ip = std::forward<decltype(res)>(res).value();
        AE_TELED_DEBUG("Server id: {}, our public ip: {}:{}, coords: {},{}",
                       server_id_, ip.ip, ip.port, ip.latitude, ip.longitude);
      } else {
        AE_TELED_ERROR("Get my ip request error {}",
                       std::forward<decltype(res)>(res).error());
      }
    });
  }});
#  endif
}

void Ping::PingResponse(RequestId request_id) {
  if (!HasActiveRequest() || request_id_ != request_id) {
    AE_TELED_WARNING("Got lost, or not our pong response");
    return;
  }

  auto current_time = Now();
  auto ping_duration =
      std::chrono::duration_cast<Duration>(current_time - request_start_);

  AE_TELED_DEBUG("Ping server id {} request {} received by {:%S} s", server_id_,
                 request_id, ping_duration);
  ResetRequestSubscriptions();
  result_event_.Emit(Ok{ping_duration});
}

void Ping::PingResponseTimeout(RequestId request_id) {
  if (!HasActiveRequest() || request_id_ != request_id) {
    AE_TELED_WARNING("Timeout for lost, or not our pong response");
    return;
  }

  AE_TELE_ERROR(kPingTimeout, "Ping server id {} request {} timeout",
                server_id_, request_id);
  ResetRequestSubscriptions();
  result_event_.Emit(Error{2});
}

void Ping::ResetRequestSubscriptions() {
  wait_result_sub_.Reset();
  timeout_sub_.Reset();
  write_sub_.Reset();
}

bool Ping::HasActiveRequest() const noexcept {
  return static_cast<bool>(wait_result_sub_);
}

}  // namespace ae
#endif  // AE_ENABLE_PING
