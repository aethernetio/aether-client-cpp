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

#include "aether/ae_actions/get_client_cloud.h"

#include <algorithm>

#include "aether/server_connections/client_server_connection.h"

#include "aether/ae_actions/ae_actions_tele.h"

namespace ae {

GetClientCloudAction::GetClientCloudAction(
    ActionContext action_context,
    ClientServerConnection& client_server_connection, Uid client_uid)
    : Action(action_context),
      action_context_{action_context},
      client_server_connection_{&client_server_connection},
      client_uid_{client_uid},
      state_{State::kRequestCloud},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELE_INFO(kGetClientCloud, "GetClientCloudAction created");

  cloud_resolved_sub_ =
      client_server_connection_->client_safe_api().send_cloud_event().Subscribe(
          [this](auto uid, auto const& cloud_descriptor) {
            if (uid == client_uid_) {
              request_cloud_task_->Stop();
              ResolveServers(cloud_descriptor);
            }
          });

  server_resolved_sub_ =
      client_server_connection_->client_safe_api()
          .send_server_descriptor_event()
          .Subscribe([this](auto const& server_descriptor) {
            auto it =
                std::find_if(std::begin(requested_cloud_),
                             std::end(requested_cloud_), [&](auto const& sid) {
                               return sid == server_descriptor.server_id;
                             });
            if (it != std::end(requested_cloud_)) {
              OnServerResponse(server_descriptor);
            }
          });
}

UpdateStatus GetClientCloudAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kRequestCloud:
        RequestCloud();
        break;
      case State::kAllServersResolved:
        return UpdateStatus::Result();
      case State::kFailed:
        return UpdateStatus::Error();
      case State::kStopped:
        return UpdateStatus::Stop();
    }
  }

  return {};
}

void GetClientCloudAction::Stop() {
  state_.Set(State::kStopped);

  if (request_cloud_task_) {
    request_cloud_task_->Stop();
  }
  if (server_resolve_task_) {
    server_resolve_task_->Stop();
  }
}

std::map<ServerId, ServerDescriptor> const&
GetClientCloudAction::server_descriptors() const {
  return server_descriptors_;
}

void GetClientCloudAction::RequestCloud() {
  AE_TELE_INFO(kGetClientCloudRequestCloud, "RequestCloud for uid {}",
               client_uid_);

  auto repeat_count =
      client_server_connection_->stream().stream_info().is_reliable ? 1 : 5;

  // Repeat request's until get the answear
  request_cloud_task_ = ActionPtr<RepeatableTask>{
      action_context_,
      [this]() {
        client_server_connection_->AuthorizedApiCall(
            SubApi{[this](ApiContext<AuthorizedApi>& auth_api) {
              auth_api->resolver_clouds({client_uid_});
            }});
      },
      // TODO: add config for repeat time and count
      std::chrono::milliseconds{500},
      repeat_count,
  };

  cloud_request_sub_ =
      request_cloud_task_->StatusEvent().Subscribe(OnError{[this]() {
        AE_TELED_ERROR("Cloud request count exceeded");
        state_ = State::kFailed;
      }});
}

void GetClientCloudAction::ResolveServers(
    CloudDescriptor const& cloud_descriptor) {
  requested_cloud_ = cloud_descriptor.sids;

  auto repeat_count =
      client_server_connection_->stream().stream_info().is_reliable ? 1 : 5;

  server_resolve_task_ = ActionPtr<RepeatableTask>{
      action_context_,
      [this]() {
        client_server_connection_->AuthorizedApiCall(
            SubApi{[this](ApiContext<AuthorizedApi>& auth_api) {
              AE_TELED_DEBUG("Resolve servers {}", requested_cloud_);
              auth_api->resolver_servers(requested_cloud_);
            }});
      },
      // TODO: add config for repeat time and count
      std::chrono::milliseconds{500},
      repeat_count,
  };

  servers_resolve_sub_ =
      server_resolve_task_->StatusEvent().Subscribe(OnError{[this]() {
        AE_TELED_ERROR("Server resolve count exceeded");
        state_ = State::kFailed;
      }});
}

void GetClientCloudAction::OnServerResponse(
    ServerDescriptor const& server_descriptor) {
  AE_TELE_DEBUG(kGetClientCloudServerResolved,
                "Server resolved id: {} ips count {}",
                server_descriptor.server_id, server_descriptor.ips.size());
  server_resolve_task_->Stop();

  server_descriptors_.emplace(server_descriptor.server_id, server_descriptor);
  if (server_descriptors_.size() == requested_cloud_.size()) {
    state_.Set(State::kAllServersResolved);
    auto duration =
        std::chrono::duration_cast<Duration>(Now() - start_resolve_);
    AE_TELED_DEBUG("Cloud and servers resolved by {:%S}", duration);
  }
}
}  // namespace ae
