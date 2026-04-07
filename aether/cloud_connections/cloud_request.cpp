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

#include <vector>
#include <utility>

#include "aether/server.h"
#include "aether/aether.h"
#include "aether/reflect/reflect.h"
#include "aether/write_action/write_action.h"
#include "aether/cloud_connections/cloud_visit.h"

#include "aether/cloud_connections/cloud_connections_tele.h"

namespace ae {
CloudRequest::ReplicaWA::ReplicaWA(std::vector<WriteAction*>&& swas) noexcept
    : swas_{std::move(swas)} {
  assert(!swas_.empty());
  for (auto* action : swas_) {
    // get the state from replicas by OR
    // any success or all ether failed
    subs_ +=
        action->status_event().Subscribe([this](WriteAction::Status status) {
          switch (status) {
            case WriteAction::Status::kSuccess:
              SetStatus(status);
              return;
            case WriteAction::Status::kFail:
              failed_actions_++;
              break;
            case WriteAction::Status::kStop:
              stopped_actions_++;
              break;
          }
          if ((failed_actions_ + stopped_actions_) >= swas_.size()) {
            SetStatus(status);
          }
        });
  }
}

void CloudRequest::ReplicaWA::Stop() noexcept {
  for (auto* action : swas_) {
    action->Stop();
  }
}

CloudRequest::CloudRequest(AeContext const& ae_context,
                           CloudServerConnections& connection)
    : ae_context_{ae_context}, connection_{&connection} {}

WriteAction& CloudRequest::CallApi(AuthApiCaller const& api_caller,
                                   RequestPolicy::Variant policy) {
  std::vector<WriteAction*> swas;
  CloudVisit::Visit(
      [&](CloudServerConnection* sc) {
        auto* conn = sc->client_connection();
        assert((conn != nullptr) && "Client connection is null");
        swas.emplace_back(&conn->AuthorizedApiCall(
            SubApi<AuthorizedApi>{[&](auto& api) { api_caller(api, sc); }}));
      },
      *connection_, policy);

  if (swas.empty()) {
    return EmptyWriteAction();
  }
  if (swas.size() == 1) {
    return *swas.front();
  }
  return ReplicaWriteAction(std::move(swas));
}

WriteAction& CloudRequest::EmptyWriteAction() {
  if (!empty_wa_ || empty_wa_->is_finished()) {
    empty_wa_.emplace(ae_context_);
  }
  return *empty_wa_;
}

WriteAction& CloudRequest::ReplicaWriteAction(
    std::vector<WriteAction*>&& swas) {
  // TODO: replace vector to something static
  return replica_was_.emplace_back(std::move(swas));
}

CloudRequestAction::CloudRequestAction(
    AeContext const& ae_context, AuthApiCaller&& api_caller,
    ClientResponseListener&& listener,
    CloudServerConnections& cloud_server_connections,
    RequestPolicy::Variant policy)
    : Action{ae_context.aether()},
      request_{std::move(api_caller)},
      listener_{std::move(listener)},
      cloud_sc_{&cloud_server_connections},
      policy_{policy},
      cloud_sub_{
          ClientListener{
              [this](auto& api, auto* sc) { return listener_(api, sc, this); },
          },
          *cloud_sc_,
          policy_,
      },
      state_{State::kMakeRequest} {
  state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
  server_changed_sub_ = cloud_sc_->servers_update_event().Subscribe(
      MethodPtr<&CloudRequestAction::ServersUpdated>{this});
}

CloudRequestAction::CloudRequestAction(
    AeContext const& ae_context, AuthApiRequest&& api_request,
    CloudServerConnections& cloud_server_connections,
    RequestPolicy::Variant policy)
    : Action{ae_context.aether()},
      request_{std::move(api_request)},
      cloud_sc_{&cloud_server_connections},
      policy_{policy},
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
  return {};
}

void CloudRequestAction::Stop() { state_ = State::kStopped; }
void CloudRequestAction::Succeeded() { state_ = State::kResult; }
void CloudRequestAction::Failed() { state_ = State::kFailed; }

void CloudRequestAction::MakeRequest(TimePoint current_time) {
  CloudVisit::Visit([&](auto* sc) { MakeRequest(current_time, sc); },
                    *cloud_sc_, policy_);
  state_ = State::kWait;
}

void CloudRequestAction::MakeRequest([[maybe_unused]] TimePoint current_time,
                                     CloudServerConnection* sc) {
  auto* conn = sc->client_connection();
  assert((conn != nullptr) && "Client connection is null");

  auto* sr = SaveRequest(sc);
  // if new request was not created skip server
  if (sr == nullptr) {
    return;
  }

  AE_TELED_DEBUG("Make request to server {}", sc->server()->server_id);

  // make request depends on saved request kind
  auto& swa = std::visit(  // ~(^.^)~
      reflect::OverrideFunc{
          // Plain AuthApiCaller with ClientResponseListener
          [&](AuthApiCaller& api_caller) -> decltype(auto) {
            return conn->AuthorizedApiCall(SubApi{
                [&](ApiContext<AuthorizedApi>& api) { api_caller(api, sc); }});
          },
          // AuthApiRequest
          [&](AuthApiRequest& api_request) -> decltype(auto) {
            return conn->AuthorizedApiCall(
                SubApi{[&](ApiContext<AuthorizedApi>& api) {
                  api_request(api, sc, this);
                }});
          },
      },
      request_);

  sr->state_subs.Push(
      // if server stream changed it's channel
      conn->server_connection().channel_changed_event().Subscribe([this, sc]() {
        AE_TELED_WARNING("Request server channel changed");
        RemoveRequest(sc);
        state_ = State::kMakeRequest;
      }),
      // if server stream is disconnected
      conn->server_connection().server_error_event().Subscribe([this, sc]() {
        AE_TELED_WARNING("Request server error");
        RemoveRequest(sc);
        state_ = State::kMakeRequest;
      }),
      // if request write failed
      swa.status_event().Subscribe([this, sc](auto status) {
        if (status == WriteAction::Status::kFail) {
          AE_TELED_ERROR("Request write error");
          RemoveRequest(sc);
          state_ = State::kMakeRequest;
        }
      }));
  // TODO: add request server level timeout
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
    CloudServerConnection* server_connection) {
  auto& sr = server_requests_[server_connection];
  // request is already active
  if (sr.is_active) {
    return nullptr;
  }
  // activate request
  sr.is_active = true;
  return &sr;
}

void CloudRequestAction::RemoveRequest(
    CloudServerConnection* server_connection) {
  auto& sr = server_requests_[server_connection];
  sr.is_active = false;
  sr.state_subs.Reset();
}

}  // namespace ae
