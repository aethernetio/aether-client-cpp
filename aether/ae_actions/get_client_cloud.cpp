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

#include "aether/ae_actions/ae_actions_tele.h"

namespace ae {

GetClientCloudAction::GetClientCloudAction(
    ActionContext action_context, Uid client_uid,
    CloudConnection& cloud_connection, RequestPolicy::Variant request_policy)
    : Action(action_context),
      action_context_{action_context},
      client_uid_{client_uid},
      cloud_connection_{&cloud_connection},
      request_policy_{request_policy},
      state_{State::kRequestCloud} {
  AE_TELE_INFO(kGetClientCloud, "GetClientCloudAction created");
  state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });

  cloud_resolved_sub_ = cloud_connection_->ClientApiSubscription(
      [this](ClientApiSafe& client_api, auto*) {
        return client_api.send_cloud_event().Subscribe(
            [this](auto const& uid, auto const& cloud_descriptor) {
              if (uid == client_uid_) {
                cloud_ = cloud_descriptor.sids;
                AE_TELED_DEBUG("Cloud resolved as {}", cloud_);
                request_cloud_task_->Stop();
                state_ = State::kResult;
              }
            });
      },
      request_policy_);
}

UpdateStatus GetClientCloudAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kRequestCloud:
        RequestCloud();
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

void GetClientCloudAction::Stop() {
  state_.Set(State::kStopped);

  if (request_cloud_task_) {
    request_cloud_task_->Stop();
  }
}

std::vector<ServerId> const& GetClientCloudAction::cloud() const {
  return cloud_;
}

void GetClientCloudAction::RequestCloud() {
  AE_TELE_INFO(kGetClientCloudRequestCloud, "RequestCloud for uid {}",
               client_uid_);

  // TODO: add config for repeat time and count
  auto repeat_count = 5;

  // Repeat request's until get the answear
  request_cloud_task_ = OwnActionPtr<RepeatableTask>{
      action_context_,
      [this]() {
        cloud_connection_->AuthorizedApiCall(
            [this](ApiContext<AuthorizedApi>& auth_api, auto*) {
              AE_TELED_DEBUG("Resolve cloud for {}", client_uid_);
              auth_api->resolver_clouds({client_uid_});
            },
            request_policy_);
      },
      std::chrono::milliseconds{1000},
      repeat_count,
  };

  cloud_request_sub_ =
      request_cloud_task_->StatusEvent().Subscribe(OnError{[this]() {
        AE_TELED_ERROR("Cloud request count exceeded");
        state_ = State::kFailed;
      }});
}
}  // namespace ae
