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

#include "aether/cloud_connections/cloud_request.h"

#include <utility>
#include <algorithm>

#include "aether/server.h"
#include "aether/reflect/reflect.h"
#include "aether/cloud_connections/cloud_visit.h"
#include "aether/stream_api/stream_write_action.h"

namespace ae {
namespace cloud_request_internal {
class EmptyConnectionsSWA final : public StreamWriteAction {
 public:
  explicit EmptyConnectionsSWA(ActionContext action_context)
      : StreamWriteAction(action_context) {
    state_ = State::kFailed;
  }
};

class ReplicaSWA final : public StreamWriteAction {
 public:
  ReplicaSWA(ActionContext action_context,
             std::vector<ActionPtr<StreamWriteAction>> swas)
      : StreamWriteAction(action_context), swas_(std::move(swas)) {
    assert(!swas_.empty());
    for (auto& action : swas_) {
      // get the state from replicas by OR
      // any success or all ether failed
      subs_ += action->StatusEvent().Subscribe(
          [this](ActionEventStatus<StreamWriteAction> const& status) {
            if (status.result().type == UpdateStatusType::kResult) {
              Status(*this, UpdateStatus::Result());
            } else {
              failed_actions_++;
              last_status_ = status.result();
              if (failed_actions_ == swas_.size()) {
                Status(*this, last_status_);
              }
            }
          });
    }
  }

  void Stop() override {
    for (auto& action : swas_) {
      action->Stop();
    }
  }

 private:
  std::vector<ActionPtr<StreamWriteAction>> swas_;
  std::size_t failed_actions_{};
  UpdateStatus last_status_;
  MultiSubscription subs_;
};
}  // namespace cloud_request_internal

ActionPtr<StreamWriteAction> CloudRequest::CallApi(
    AuthApiCaller const& api_caller, CloudServerConnections& connection,
    RequestPolicy::Variant policy) {
  std::vector<ActionPtr<StreamWriteAction>> swas;
  CloudVisit::Visit(
      [&](ServerConnection* sc) {
        auto* conn = sc->ClientConnection();
        assert((conn != nullptr) && "Client connection is null");
        swas.emplace_back(conn->AuthorizedApiCall(
            SubApi<AuthorizedApi>{[&](auto& api) { api_caller(api, sc); }}));
      },
      connection, policy);

  if (swas.empty()) {
    return ActionPtr<cloud_request_internal::EmptyConnectionsSWA>{
        connection.action_context_};
  }
  if (swas.size() == 1) {
    return swas.front();
  }
  return ActionPtr<cloud_request_internal::ReplicaSWA>{
      connection.action_context_, std::move(swas)};
}

CloudRequestAction::CloudRequestAction(
    ActionContext action_context, AuthApiCaller&& api_caller,
    ClientResponseListener&& listener,
    CloudServerConnections& cloud_server_connections,
    RequestPolicy::Variant policy, Duration request_timeout, int max_attempts)
    : Action{action_context},
      request_{std::move(api_caller)},
      listener_{std::move(listener)},
      cloud_sc_{&cloud_server_connections},
      policy_{policy},
      request_timeout_{request_timeout},
      max_attempts_{max_attempts},
      cloud_sub_{
          [this](auto& api, auto* sc) { return listener_(api, sc, this); },
          *cloud_sc_, policy_},
      state_{State::kMakeRequest} {
  state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
  server_changed_sub_ = cloud_sc_->servers_update_event().Subscribe(
      MethodPtr<&CloudRequestAction::ServersUpdated>{this});
}

CloudRequestAction::CloudRequestAction(
    ActionContext action_context, AuthApiRequest&& api_request,
    CloudServerConnections& cloud_server_connections,
    RequestPolicy::Variant policy, Duration request_timeout, int max_attempts)
    : Action{action_context},
      request_{std::move(api_request)},
      cloud_sc_{&cloud_server_connections},
      policy_{policy},
      request_timeout_{request_timeout},
      max_attempts_{max_attempts},
      state_{State::kMakeRequest} {
  state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
  server_changed_sub_ = cloud_sc_->servers_update_event().Subscribe(
      MethodPtr<&CloudRequestAction::ServersUpdated>{this});
}

UpdateStatus CloudRequestAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kMakeRequest:
        MakeRequest(current_time);
        break;
      case State::kWait:
        break;
      case State::kResult:
        return UpdateStatus::Result();
      case State::kFailed:
        return UpdateStatus::Error();
      case State::kStopped:
        return UpdateStatus::Stop();
    }
  }
  if (state_ == State::kWait) {
    return Wait(current_time);
  }
  return {};
}

void CloudRequestAction::Stop() { state_ = State::kStopped; }
void CloudRequestAction::Succeeded() { state_ = State::kResult; }
void CloudRequestAction::Failed() { state_ = State::kFailed; }

void CloudRequestAction::MakeRequest(TimePoint current_time) {
  // compare max attempts with request with minimum attempts
  auto min = std ::min_element(
      std::begin(server_requests_), std::end(server_requests_),
      [](auto const& left, auto const& right) {
        return left.second.attempts < right.second.attempts;
      });
  if (min != std::end(server_requests_)) {
    if (min->second.attempts >= max_attempts_) {
      AE_TELED_ERROR("Max attempts reached");
      state_ = State::kFailed;
      return;
    }
  }

  CloudVisit::Visit([&](auto* sc) { MakeRequest(current_time, sc); },
                    *cloud_sc_, policy_);
  state_ = State::kWait;
}

UpdateStatus CloudRequestAction::Wait(TimePoint current_time) {
  TimePoint min_tp = current_time;
  for (auto& [sc, sr] : server_requests_) {
    min_tp = std::min(min_tp, sr.timeout);
    if (sr.is_active && (sr.timeout <= current_time)) {
      AE_TELED_ERROR("Request timeout");
      sc->Restream();
      RemoveRequest(sc);
      state_ = State::kMakeRequest;
    }
  }

  if (state_ == State::kWait) {
    return UpdateStatus::Delay(min_tp);
  }

  return {};
}

void CloudRequestAction::MakeRequest(TimePoint current_time,
                                     ServerConnection* server_connection) {
  auto* conn = server_connection->ClientConnection();
  assert((conn != nullptr) && "Client connection is null");

  auto* sr = SaveRequest(server_connection);
  if (sr == nullptr) {
    return;
  }
  sr->timeout = current_time + request_timeout_;
  sr->attempts++;

  AE_TELED_DEBUG("Make request to server {}, attempt {}",
                 server_connection->server()->server_id, sr->attempts);

  // make request depends on saved request kind
  auto swa = std::visit(  // ~(^.^)~
      reflect::OverrideFunc{
          // Plain AuthApiCaller with ClientResponseListener
          [&](AuthApiCaller& api_caller) {
            return conn->AuthorizedApiCall(
                SubApi{[&](ApiContext<AuthorizedApi>& api) {
                  api_caller(api, server_connection);
                }});
          },
          // AuthApiRequest
          [&](AuthApiRequest& api_request) {
            return conn->AuthorizedApiCall(
                SubApi{[&](ApiContext<AuthorizedApi>& api) {
                  api_request(api, server_connection, this);
                }});
          },
      },
      request_);

  // if server stream is disconnected
  sr->state_subs +=
      conn->stream_update_event().Subscribe([this, conn, server_connection]() {
        if (conn->stream_info().link_state == LinkState::kLinkError) {
          RemoveRequest(server_connection);
          state_ = State::kMakeRequest;
        }
      });

  // if request write failed
  sr->state_subs +=
      swa->StatusEvent().Subscribe(OnError{[this, server_connection]() {
        AE_TELED_ERROR("Request write error");
        server_connection->Restream();
        RemoveRequest(server_connection);
        state_ = State::kMakeRequest;
      }});
}

void CloudRequestAction::ServersUpdated() {
  if ((state_ != State::kMakeRequest) && state_ != State::kWait) {
    return;
  }
  // reset all requests
  for (auto& [_, sr] : server_requests_) {
    sr.is_active = false;
    sr.state_subs.Reset();
  }
  state_ = State::kMakeRequest;
}

CloudRequestAction::ServerRequest* CloudRequestAction::SaveRequest(
    ServerConnection* server_connection) {
  auto& sr = server_requests_[server_connection];
  // request is already active
  if (sr.is_active) {
    return nullptr;
  }
  // activate request
  sr.is_active = true;
  return &sr;
}

void CloudRequestAction::RemoveRequest(ServerConnection* server_connection) {
  auto& sr = server_requests_[server_connection];
  sr.is_active = false;
  sr.state_subs.Reset();
}

}  // namespace ae
