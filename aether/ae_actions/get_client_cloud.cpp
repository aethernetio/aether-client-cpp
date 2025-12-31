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
    CloudServerConnections& cloud_connection,
    RequestPolicy::Variant request_policy)
    : Action(action_context),
      client_uid_{client_uid},
      state_{State::kNone},
      cloud_request_{
          action_context,
          AuthApiCaller{[this](ApiContext<AuthorizedApi>& auth_api, auto*) {
            AE_TELED_DEBUG("Resolve cloud for {}", client_uid_);
            auth_api->resolver_clouds({client_uid_});
          }},
          ClientResponseListener{[this](ClientApiSafe& client_api, auto*,
                                        auto* request) {
            return client_api.send_cloud_event().Subscribe(
                [this, request](auto const& uid, auto const& cloud_descriptor) {
                  if (uid == client_uid_) {
                    cloud_ = cloud_descriptor.sids;
                    request->Succeeded();
                  }
                });
          }},
          cloud_connection,
          request_policy,
      } {
  AE_TELE_INFO(kGetClientCloud, "GetClientCloudAction created");
  cloud_request_sub_ = cloud_request_->StatusEvent().Subscribe(ActionHandler{
      OnResult{[this]() {
        AE_TELED_DEBUG("Cloud resolved as [{}]", cloud_);
        state_ = State::kResult;
        Action::Trigger();
      }},
      OnError{[this]() {
        AE_TELED_ERROR("Cloud resolution failed");
        state_ = State::kFailed;
        Action::Trigger();
      }},
  });
}

UpdateStatus GetClientCloudAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kResult:
        return UpdateStatus::Result();
      case State::kFailed:
        return UpdateStatus::Error();
      case State::kStopped:
        return UpdateStatus::Stop();
      default:
        break;
    }
  }
  return {};
}

void GetClientCloudAction::Stop() {
  cloud_request_->Stop();
  state_ = State::kStopped;
  Action::Trigger();
}

std::vector<ServerId> const& GetClientCloudAction::cloud() const {
  return cloud_;
}

}  // namespace ae
