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

#include "aether/ae_actions/get_servers.h"

#include <algorithm>

#include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
GetServersAction::GetServersAction(ActionContext action_context,
                                   std::vector<ServerId> server_ids,
                                   CloudServerConnections& cloud_connection,
                                   RequestPolicy::Variant request_policy)
    : Action{action_context},
      server_ids_{std::move(server_ids)},
      state_{State::kNone},
      cloud_request_{
          action_context,
          AuthApiCaller{[this](ApiContext<AuthorizedApi>& auth_api, auto*) {
            AE_TELED_DEBUG("Resolve servers {}", server_ids_);
            auth_api->resolver_servers(server_ids_);
          }},
          ClientResponseListener{[this](ClientApiSafe& client_api, auto*,
                                        auto* request) {
            return client_api.send_server_descriptor_event().Subscribe(
                [this, request](auto const& sd) { GetResponse(sd, request); });
          }},
          cloud_connection,
          request_policy,
      } {
  request_sub_ = cloud_request_->StatusEvent().Subscribe(ActionHandler{
      OnResult{[this]() {
        state_ = State::kResult;
        Action::Trigger();
      }},
      OnError{[this]() {
        state_ = State::kFailed;
        Action::Trigger();
      }},
  });
}

UpdateStatus GetServersAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kResult:
        return UpdateStatus::Result();
      case State::kFailed:
        return UpdateStatus::Error();
      case State::kStop:
        return UpdateStatus::Stop();
      default:
        break;
    }
  }
  return {};
}

std::vector<ServerDescriptor> const& GetServersAction::servers() const {
  return server_descriptors_;
}

void GetServersAction::Stop() {
  cloud_request_->Stop();
  state_ = State::kStop;
  Action::Trigger();
}

void GetServersAction::GetResponse(ServerDescriptor const& server_descriptor,
                                   CloudRequestAction* request) {
  // If got not requested server id ignore it.
  if (auto it = std::find_if(std::begin(server_ids_), std::end(server_ids_),
                             [sid{server_descriptor.server_id}](ServerId id) {
                               return id == sid;
                             });
      it == std::end(server_ids_)) {
    return;
  }

  // insert if not inserted
  if (auto it = std::find_if(
          std::begin(server_descriptors_), std::end(server_descriptors_),
          [sid{server_descriptor.server_id}](ServerDescriptor const& d) {
            return d.server_id == sid;
          });
      it != std::end(server_descriptors_)) {
    return;
  }
  server_descriptors_.push_back(server_descriptor);
  if (server_descriptors_.size() == server_ids_.size()) {
    request->Succeeded();
  }
}
}  // namespace ae
