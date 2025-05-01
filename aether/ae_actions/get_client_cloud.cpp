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

#include <utility>

#include "aether/api_protocol/api_context.h"

#include "aether/methods/work_server_api/authorized_api.h"

#include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
GetClientCloudAction::GetClientCloudAction(
    ActionContext action_context, ClientToServerStream& client_to_server_stream,
    Uid client_uid)
    : Action(action_context),
      client_to_server_stream_{&client_to_server_stream},
      client_uid_{client_uid},
      state_{State::kRequestCloud},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELE_INFO(kGetClientCloud, "GetClientCloudAction created");

  auto server_stream_id = StreamIdGenerator::GetNextClientStreamId();
  auto cloud_stream_id = StreamIdGenerator::GetNextClientStreamId();

  auto auth_api = ApiContext{client_to_server_stream_->protocol_context(),
                             client_to_server_stream_->authorized_api()};
  auth_api->resolvers(server_stream_id, cloud_stream_id);

  add_resolvers_.emplace(DataBuffer{std::move(auth_api)});

  server_resolver_stream_.emplace(
      SerializeGate<ServerId, ServerDescriptor>{},
      StreamApiGate{client_to_server_stream_->protocol_context(),
                    server_stream_id},
      *add_resolvers_);

  cloud_request_stream_.emplace(
      SerializeGate<Uid, UidAndCloud>{},
      StreamApiGate{client_to_server_stream_->protocol_context(),
                    cloud_stream_id},
      *add_resolvers_);

  Tie(*cloud_request_stream_, *client_to_server_stream_);
  Tie(*server_resolver_stream_, *client_to_server_stream_);

  cloud_response_subscription_ =
      cloud_request_stream_->out_data_event().Subscribe(
          [this](auto const& data) { OnCloudResponse(data); });

  server_resolve_subscription_ =
      server_resolver_stream_->out_data_event().Subscribe(
          [this](auto const& data) { OnServerResponse(data); });
  start_resolve_ = Now();
}

TimePoint GetClientCloudAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kRequestCloud:
        RequestCloud(current_time);
        break;
      case State::kRequestServerResolve:
        RequestServerResolve(current_time);
        break;
      case State::kAllServersResolved:
        Action::Result(*this);
        break;
      case State::kFailed:
        Action::Error(*this);
        break;
      case State::kStopped:
        Action::Stop(*this);
        break;
    }
  }

  return current_time;
}

void GetClientCloudAction::Stop() {
  state_.Set(State::kStopped);

  if (cloud_request_action_) {
    cloud_request_action_->Stop();
  }
  for (auto& action : server_resolve_actions_) {
    action->Stop();
  }
}

std::vector<ServerDescriptor> const&
GetClientCloudAction::server_descriptors() {
  return server_descriptors_;
}

void GetClientCloudAction::RequestCloud(TimePoint current_time) {
  AE_TELE_INFO(kGetClientCloudRequestCloud,
               "RequestCloud for uid {} at {:%H:%M:%S}", client_uid_,
               current_time);

  cloud_request_action_ = cloud_request_stream_->Write(Uid{client_uid_});

  cloud_request_subscriptions_.Push(
      cloud_request_action_->StopEvent().Subscribe(
          [this](auto const&) { state_.Set(State::kFailed); }),
      cloud_request_action_->ErrorEvent().Subscribe(
          [this](auto const&) { state_.Set(State::kFailed); }));
}

void GetClientCloudAction::RequestServerResolve(TimePoint current_time) {
  // TODO: use server cache

  AE_TELE_DEBUG(kGetClientCloudRequestServerResolve,
                "RequestServerResolve for ids {} at {:%H:%M:%S}",
                uid_and_cloud_.cloud, current_time);

  server_resolve_actions_.reserve(uid_and_cloud_.cloud.size());
  for (auto server_id : uid_and_cloud_.cloud) {
    auto swa = server_resolver_stream_->Write(std::move(server_id));
    server_resolve_subscriptions_.Push(
        swa->StopEvent().Subscribe([this, server_id](auto const&) {
          AE_TELE_ERROR(kGetClientCloudServerResolveStopped,
                        "Resolve server id {} stopped", server_id);
          state_.Set(State::kFailed);
        }),
        swa->ErrorEvent().Subscribe([this, server_id](auto const&) {
          AE_TELE_ERROR(kGetClientCloudServerResolveFailed,
                        "Resolve server id {} failed", server_id);
          state_.Set(State::kFailed);
        }));
    server_resolve_actions_.emplace_back(std::move(swa));
  }
}

void GetClientCloudAction::OnCloudResponse(UidAndCloud const& uid_and_cloud) {
  uid_and_cloud_ = uid_and_cloud;
  state_.Set(State::kRequestServerResolve);
}

void GetClientCloudAction::OnServerResponse(
    ServerDescriptor const& server_descriptor) {
  AE_TELE_DEBUG(kGetClientCloudServerResolved,
                "Server resolved {} ips count {}", server_descriptor.server_id,
                server_descriptor.ips.size());
  server_descriptors_.push_back(server_descriptor);
  if (server_descriptors_.size() == uid_and_cloud_.cloud.size()) {
    server_resolve_actions_.clear();
    state_.Set(State::kAllServersResolved);
    auto duration =
        std::chrono::duration_cast<Duration>(Now() - start_resolve_);
    AE_TELED_DEBUG("Cloud and servers resolved by {:%S}", duration);
  }
}
}  // namespace ae
