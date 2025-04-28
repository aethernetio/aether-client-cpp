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

#include "aether/ae_actions/get_client_cloud_connection.h"

#include <utility>

#include "aether/client.h"
#include "aether/server_list/no_filter_server_list_policy.h"
#include "aether/client_connections/client_to_server_stream.h"
#include "aether/client_connections/client_cloud_connection.h"
#include "aether/client_connections/client_connection_manager.h"

#include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
GetClientCloudConnection::GetClientCloudConnection(
    ActionContext action_context,
    Ptr<ClientConnectionManager> const& client_connection_manager,
    ObjPtr<Client> const& client, Uid client_uid, ObjPtr<Cloud> const& cloud,
    std::unique_ptr<IServerConnectionFactory>&& server_connection_factory)
    : Action{action_context},
      action_context_{action_context},
      client_{client},
      client_uid_{client_uid},
      client_connection_manager_{client_connection_manager},
      server_connection_selector_{cloud,
                                  make_unique<NoFilterServerListPolicy>(),
                                  std::move(server_connection_factory)},
      state_{State::kTryCache},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELE_DEBUG(kGetClientCloudConnection, "GetClientCloudConnection created");
}

GetClientCloudConnection::~GetClientCloudConnection() {
  AE_TELE_DEBUG(kGetClientCloudConnectionDestroyed);
}

TimePoint GetClientCloudConnection::Update(TimePoint current_time) {
  AE_TELED_DEBUG("Update() state_ {}", state_.get());

  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kTryCache:
        TryCache(current_time);
        break;
      case State::kSelectConnection:
        SelectConnection(current_time);
        break;
      case State::kConnection:
        break;
      case State::kGetCloud:
        GetCloud(current_time);
        break;
      case State::kCreateConnection:
        CreateConnection(current_time);
        break;
      case State::kSuccess:
        Action::Result(*this);
        break;
      case State::kStopped:
        Action::Stop(*this);
        break;
      case State::kFailed:
        Action::Error(*this);
        break;
    }
  }

  return current_time;
}

void GetClientCloudConnection::Stop() {
  if (get_client_cloud_action_) {
    get_client_cloud_action_->Stop();
  }
  state_ = State::kStopped;
}

Ptr<ClientConnection> GetClientCloudConnection::client_cloud_connection() {
  return std::move(client_cloud_connection_);
}

void GetClientCloudConnection::TryCache(TimePoint /* current_time */) {
  // TODO: add get cloud connection from cache

  connection_selection_loop_ =
      AsyncForLoop<RcPtr<ClientServerConnection>>::Construct(
          server_connection_selector_,
          [this]() { return server_connection_selector_.GetConnection(); });
  state_ = State::kSelectConnection;
}

void GetClientCloudConnection::SelectConnection(TimePoint /* current_time */) {
  if (server_connection_ = connection_selection_loop_->Update();
      !server_connection_) {
    AE_TELE_ERROR(kGetClientCloudConnectionServerListIsOver,
                  "Server connection list is over");
    state_ = State::kFailed;
    return;
  }

  if (server_connection_->server_stream().in().stream_info().is_linked) {
    state_ = State::kGetCloud;
    return;
  }

  connection_subscription_ =
      server_connection_->server_stream().in().gate_update_event().Subscribe(
          [this]() {
            if (server_connection_->server_stream()
                    .in()
                    .stream_info()
                    .is_linked) {
              state_ = State::kGetCloud;
            } else {
              state_ = State::kSelectConnection;
            }
          });

  // wait connection
  state_ = State::kConnection;
}

void GetClientCloudConnection::GetCloud(TimePoint /* current_time */) {
  get_client_cloud_action_.emplace(
      action_context_, server_connection_->server_stream(), client_uid_);

  get_client_cloud_subscriptions_.Push(
      get_client_cloud_action_->ErrorEvent().Subscribe(
          [this](auto const&) { state_ = State::kSelectConnection; }),
      get_client_cloud_action_->ResultEvent().Subscribe(
          [this](auto const&) { state_ = State::kCreateConnection; }),
      get_client_cloud_action_->StopEvent().Subscribe(
          [this](auto const&) { state_ = State::kStopped; }));
}

void GetClientCloudConnection::CreateConnection(TimePoint /* current_time */) {
  assert(get_client_cloud_action_.has_value());

  auto servers = get_client_cloud_action_->server_descriptors();

  auto ccm = client_connection_manager_.Lock();
  auto cloud = ccm->RegisterCloud(client_uid_, servers);
  assert(cloud);

  client_cloud_connection_ = ccm->CreateClientConnection(cloud);
  state_ = State::kSuccess;
}

}  // namespace ae
