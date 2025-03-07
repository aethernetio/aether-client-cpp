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

#include "aether/stream_api/stream_api.h"
#include "aether/stream_api/tied_stream.h"
#include "aether/stream_api/protocol_stream.h"
#include "aether/stream_api/serialize_stream.h"

#include "aether/methods/work_server_api/authorized_api.h"
#include "aether/methods/client_api/client_safe_api.h"

#include "aether/tele/tele.h"

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
  AE_TELED_INFO("GetClientCloudAction created");

  auto server_stream_id = StreamIdGenerator::GetNextClientStreamId();
  auto cloud_stream_id = StreamIdGenerator::GetNextClientStreamId();

  pre_client_to_server_stream_ = make_unique<TiedStream>(
      ProtocolReadGate{protocol_context_, ClientSafeApi{}},
      ProtocolWriteGate{
          protocol_context_, AuthorizedApi{},
          AuthorizedApi::Resolvers{{}, server_stream_id, cloud_stream_id}});

  Tie(*pre_client_to_server_stream_, *client_to_server_stream_);

  server_resolver_stream_ = make_unique<TiedStream>(
      SerializeGate<ServerId, ServerDescriptor>{},
      StreamApiGate{protocol_context_, server_stream_id});

  cloud_request_stream_ = make_unique<TiedStream>(
      SerializeGate<Uid, UidAndCloud>{},
      StreamApiGate{protocol_context_, cloud_stream_id});

  Tie(*cloud_request_stream_, *pre_client_to_server_stream_);
  Tie(*server_resolver_stream_, *pre_client_to_server_stream_);

  cloud_response_subscription_ =
      cloud_request_stream_->in().out_data_event().Subscribe(
          [this](auto const& data) { OnCloudResponse(data); });

  server_resolve_subscription_ =
      server_resolver_stream_->in().out_data_event().Subscribe(
          [this](auto const& data) { OnServerResponse(data); });
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
      case State::kStoped:
        Action::Stop(*this);
        break;
    }
  }

  return current_time;
}

void GetClientCloudAction::Stop() {
  state_.Set(State::kStoped);

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
  AE_TELED_DEBUG("RequestCloud for uid {} at {:%H:%M:%S}", client_uid_,
                 current_time);

  cloud_request_action_ =
      cloud_request_stream_->in().Write(Uid{client_uid_}, current_time);

  cloud_request_subscriptions_.Push(
      cloud_request_action_->SubscribeOnStop(
          [this](auto const&) { state_.Set(State::kFailed); }),
      cloud_request_action_->SubscribeOnError(
          [this](auto const&) { state_.Set(State::kFailed); }));
}

void GetClientCloudAction::RequestServerResolve(TimePoint current_time) {
  // TODO: use server cache

  AE_TELED_DEBUG("RequestServerResolve for ids {} at {:%H:%M:%S}",
                 uid_and_cloud_.cloud, current_time);

  server_resolve_actions_.reserve(uid_and_cloud_.cloud.size());
  for (auto server_id : uid_and_cloud_.cloud) {
    auto swa =
        server_resolver_stream_->in().Write(std::move(server_id), current_time);
    server_resolve_subscriptions_.Push(
        swa->SubscribeOnStop([this, server_id](auto const&) {
          AE_TELED_ERROR("Resolve server id {} stopped", server_id);
          state_.Set(State::kFailed);
        }),
        swa->SubscribeOnError([this, server_id](auto const&) {
          AE_TELED_ERROR("Resolve server id {} failed", server_id);
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
  AE_TELED_DEBUG("Server resolved {} ips count {}", server_descriptor.server_id,
                 server_descriptor.ips.size());
  server_descriptors_.push_back(server_descriptor);
  if (server_descriptors_.size() == uid_and_cloud_.cloud.size()) {
    server_resolve_actions_.clear();
    state_.Set(State::kAllServersResolved);
  }
}
}  // namespace ae
