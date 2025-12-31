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
                                   CloudConnection& cloud_connection,
                                   RequestPolicy::Variant request_policy)
    : Action{action_context},
      action_context_{action_context},
      server_ids_{std::move(server_ids)},
      cloud_connection_{&cloud_connection},
      request_policy_{request_policy},
      state_{State::kMakeRequest} {
  response_sub_ = cloud_connection_->ClientApiSubscription(
      [this](ClientApiSafe& client_api, auto*) {
        return client_api.send_server_descriptor_event().Subscribe(
            MethodPtr<&GetServersAction::GetResponse>{this});
      },
      request_policy_);
  state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
}

UpdateStatus GetServersAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kMakeRequest:
        MakeRequest();
        break;
      case State::kWait:
        break;
      case State::kResult:
        return UpdateStatus::Result();
      case State::kFailed:
        return UpdateStatus::Error();
      case State::kStop:
        return UpdateStatus::Stop();
    }
  }
  if (state_ == State::kWait) {
    return Wait(current_time);
  }
  return {};
}

std::vector<ServerDescriptor> const& GetServersAction::servers() const {
  return server_descriptors_;
}

void GetServersAction::Stop() {
  repeatable_task_->Stop();
  response_sub_.Reset();
  state_ = State::kStop;
}

void GetServersAction::MakeRequest() {
  // TODO: add config for repeat time and count
  auto repeat_count = 5;

  repeatable_task_ = OwnActionPtr<RepeatableTask>{
      action_context_,
      [this]() {
        // TODO: add config for timeout
        timeout_point_ = Now() + std::chrono::seconds{10};
        cloud_connection_->AuthorizedApiCall(
            [this](ApiContext<AuthorizedApi>& auth_api, auto*) {
              AE_TELED_DEBUG("Resolve servers {}", server_ids_);
              auth_api->resolver_servers(server_ids_);
            },
            request_policy_);
      },
      // TODO: add config for repeat time and count
      std::chrono::milliseconds{10000},
      repeat_count,
  };

  request_sub_ = repeatable_task_->StatusEvent().Subscribe(OnError{[this]() {
    AE_TELED_ERROR("Server resolve request count exceeded");
    state_ = State::kFailed;
  }});

  state_ = State::kWait;
}

void GetServersAction::GetResponse(ServerDescriptor const& server_descriptor) {
  // If got not requested server id ignore it.
  if (auto it = std::find_if(std::begin(server_ids_), std::end(server_ids_),
                             [sid{server_descriptor.server_id}](ServerId id) {
                               return id == sid;
                             });
      it == std::end(server_ids_)) {
    return;
  }
  // stop send requests
  repeatable_task_->Stop();
  request_sub_.Reset();

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
    state_ = State::kResult;
  }
}

UpdateStatus GetServersAction::Wait(TimePoint current_time) {
  if (timeout_point_ >= current_time) {
    AE_TELED_ERROR("Server resolve timeout");
    state_ = State::kFailed;
    return {};
  }
  return UpdateStatus::Delay(timeout_point_);
}

}  // namespace ae
