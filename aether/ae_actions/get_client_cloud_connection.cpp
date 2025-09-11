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
#include "aether/connection_manager/client_cloud_manager.h"
#include "aether/client_connections/client_cloud_connection.h"
#include "aether/connection_manager/server_connection_manager.h"

#include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
GetClientCloudConnection::GetClientCloudConnection(ActionContext action_context,
                                                   ObjPtr<Client> const& client,
                                                   Uid client_uid)
    : Action{action_context},
      action_context_{action_context},
      client_{client},
      client_uid_{client_uid},
      state_{State::kGetCloud},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELE_DEBUG(kGetClientCloudConnection, "GetClientCloudConnection created");
}

GetClientCloudConnection::~GetClientCloudConnection() {
  AE_TELE_DEBUG(kGetClientCloudConnectionDestroyed);
}

UpdateStatus GetClientCloudConnection::Update() {
  AE_TELED_DEBUG("Update() state_ {}", state_.get());

  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kGetCloud:
        GetCloud();
        break;
      case State::kCreateConnection:
        CreateConnection();
        break;
      case State::kSuccess:
        return UpdateStatus::Result();
      case State::kStopped:
        return UpdateStatus::Stop();
      case State::kFailed:
        return UpdateStatus::Error();
    }
  }

  return {};
}

void GetClientCloudConnection::Stop() {
  if (get_cloud_action_) {
    get_cloud_action_->Stop();
  }
  state_ = State::kStopped;
}

Ptr<ClientConnection> GetClientCloudConnection::client_cloud_connection() {
  return std::move(client_cloud_connection_);
}

void GetClientCloudConnection::GetCloud() {
  auto client_ptr = client_.Lock();
  assert(client_ptr);
  get_cloud_action_ = client_ptr->cloud_manager()->GetCloud(client_uid_);
  get_cloud_sub_ = get_cloud_action_->StatusEvent().Subscribe(ActionHandler{
      OnResult{[this]() { state_ = State::kCreateConnection; }},
      OnError{[this]() { state_ = State::kFailed; }},
      OnStop{[this]() { state_ = State::kStopped; }},
  });
}

void GetClientCloudConnection::CreateConnection() {
  assert(get_cloud_action_);
  auto client_ptr = client_.Lock();
  assert(client_ptr);

  auto cloud = get_cloud_action_->cloud();

  client_cloud_connection_ = MakePtr<ClientCloudConnection>(
      action_context_, cloud,
      client_ptr->server_connection_manager().GetServerConnectionFactory());
  state_ = State::kSuccess;
}

}  // namespace ae
