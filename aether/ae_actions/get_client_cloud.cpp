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

#include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
GetClientCloudAction::AddResolversGate::AddResolversGate(
    ClientToServerStream& client_to_server_stream, StreamId server_stream_id,
    StreamId cloud_stream_id)
    : client_to_server_stream_{&client_to_server_stream},
      server_stream_id_{server_stream_id},
      cloud_stream_id_{cloud_stream_id} {}

DataBuffer GetClientCloudAction::AddResolversGate::WriteIn(
    DataBuffer&& buffer) {
  auto api = ApiContext{client_to_server_stream_->protocol_context(),
                        client_to_server_stream_->authorized_api()};
  api->resolvers(server_stream_id_, cloud_stream_id_);
  DataBuffer resolvers = std::move(api);
  buffer.insert(std::end(buffer), std::begin(resolvers), std::end(resolvers));
  return std::move(buffer);
}

GetClientCloudAction::GetClientCloudAction(
    ActionContext action_context, ClientToServerStream& client_to_server_stream,
    Uid client_uid)
    : Action(action_context),
      action_context_{action_context},
      client_to_server_stream_{&client_to_server_stream},
      client_uid_{client_uid},
      state_{State::kRequestCloud},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELE_INFO(kGetClientCloud, "GetClientCloudAction created");
  InitStreams();
}

UpdateStatus GetClientCloudAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kRequestCloud:
        RequestCloud();
        break;
      case State::kRequestServerResolve:
        RequestServerResolve();
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

  if (cloud_request_action_) {
    cloud_request_action_->Stop();
  }
  for (auto& [_, action] : server_resolve_actions_) {
    if (action) {
      action->Stop();
    }
  }
}

std::vector<ServerDescriptor> const& GetClientCloudAction::server_descriptors()
    const {
  return server_descriptors_;
}

void GetClientCloudAction::InitStreams() {
  auto server_stream_id = StreamIdGenerator::GetNextClientStreamId();
  auto cloud_stream_id = StreamIdGenerator::GetNextClientStreamId();

  add_resolvers_.emplace(*client_to_server_stream_, server_stream_id,
                         cloud_stream_id);

  cloud_request_stream_.emplace(
      SerializeGate<Uid, UidAndCloud>{},
      StreamApiGate{client_to_server_stream_->protocol_context(),
                    cloud_stream_id},
      *add_resolvers_);

  server_resolver_stream_.emplace(
      SerializeGate<ServerId, ServerDescriptor>{},
      StreamApiGate{client_to_server_stream_->protocol_context(),
                    server_stream_id},
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
  state_ = State::kRequestCloud;
}

void GetClientCloudAction::RequestCloud() {
  AE_TELE_INFO(kGetClientCloudRequestCloud, "RequestCloud for uid {}",
               client_uid_);

  auto repeat_count = cloud_request_stream_->stream_info().is_reliable ? 1 : 5;

  // Repeat request's until get the answear
  request_cloud_task_ = ActionPtr<RepeatableTask>{
      action_context_,
      [this]() {
        cloud_request_action_ = cloud_request_stream_->Write(Uid{client_uid_});
        cloud_request_subscriptions_.Push(
            cloud_request_action_->StatusEvent().Subscribe(
                ActionHandler{OnStop{[this]() {
                                request_cloud_task_->Stop();
                                state_ = State::kFailed;
                              }},
                              OnError{[this]() {
                                request_cloud_task_->Stop();
                                state_ = State::kFailed;
                              }}}));
      },
      std::chrono::milliseconds{5000}, repeat_count};
  cloud_request_subscriptions_.Push(
      request_cloud_task_->StatusEvent().Subscribe(OnError{[this]() {
        AE_TELED_ERROR("Cloud request count exceeded");
        state_ = State::kFailed;
      }}));
}

void GetClientCloudAction::RequestServerResolve() {
  // TODO: use server cache

  AE_TELE_DEBUG(kGetClientCloudRequestServerResolve,
                "RequestServerResolve for ids {}", uid_and_cloud_.cloud);

  auto repeat_count =
      server_resolver_stream_->stream_info().is_reliable ? 1 : 5;

  for (auto server_id : uid_and_cloud_.cloud) {
    auto [it, _] = server_resolve_tasks_.emplace(
        server_id,
        ActionPtr<RepeatableTask>{
            action_context_,
            [this, server_id]() {
              auto swa = server_resolver_stream_->Write(
                  std::move(ServerId{server_id}));
              server_resolve_subscriptions_.Push(
                  swa->StatusEvent().Subscribe(ActionHandler{
                      OnStop{[this, server_id]() {
                        AE_TELE_ERROR(kGetClientCloudServerResolveStopped,
                                      "Resolve server id {} stopped",
                                      server_id);
                        state_.Set(State::kFailed);
                        server_resolve_tasks_.at(server_id)->Stop();
                      }},
                      OnError{[this, server_id]() {
                        AE_TELE_ERROR(kGetClientCloudServerResolveFailed,
                                      "Resolve server id {} failed", server_id);
                        state_.Set(State::kFailed);
                        server_resolve_tasks_.at(server_id)->Stop();
                      }}}));

              server_resolve_actions_[server_id] = std::move(swa);
            },
            std::chrono::milliseconds{5000}, repeat_count});

    server_resolve_subscriptions_.Push(
        it->second->StatusEvent().Subscribe(OnError{[this, server_id]() {
          AE_TELED_ERROR("Resolve server id {} repeat count exceeded",
                         server_id);
          state_ = State::kFailed;
        }}));
  }
}

void GetClientCloudAction::OnCloudResponse(UidAndCloud const& uid_and_cloud) {
  if (request_cloud_task_) {
    request_cloud_task_->Stop();
  }
  uid_and_cloud_ = uid_and_cloud;
  state_.Set(State::kRequestServerResolve);
}

void GetClientCloudAction::OnServerResponse(
    ServerDescriptor const& server_descriptor) {
  AE_TELE_DEBUG(kGetClientCloudServerResolved,
                "Server resolved id: {} ips count {}",
                server_descriptor.server_id, server_descriptor.ips.size());
  if (auto it = server_resolve_tasks_.find(server_descriptor.server_id);
      it != std::end(server_resolve_tasks_)) {
    it->second->Stop();
    server_resolve_tasks_.erase(it);
  }

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
