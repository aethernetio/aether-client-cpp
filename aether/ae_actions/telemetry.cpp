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

#include "aether/ae_actions/telemetry.h"

#if defined TELEMETRY_ENABLED

#  include "aether/aether.h"
#  include "aether/format/format.h"
#  include "aether/tele/traps/statistics_trap.h"

#  include "aether/mstream.h"
#  include "aether/mstream_buffers.h"
#  include "aether/cloud_connections/cloud_request.h"

#  include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
Telemetry::Telemetry(ActionContext action_context, Ptr<Aether> const& aether,
                     CloudServerConnections& cloud_connection)
    : Action{action_context},
      aether_{aether},
      cloud_connection_{&cloud_connection},
      telemetry_request_sub_{
          ClientListener{[&](ClientApiSafe& api, auto* sever_connect) {
            return api.request_telemetry_event().Subscribe(
                [&]() { OnRequestTelemetry(sever_connect->priority()); });
          }},
          *cloud_connection_,
          RequestPolicy::Replica{cloud_connection_->max_connections()}},
      state_{State::kWaitRequest} {
  AE_TELE_INFO(TelemetryCreated);
}

UpdateStatus Telemetry::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaitRequest:
        break;
      case State::kSendTelemetry:
        SendTelemetry();
        break;
      case State::kStopped:
        return UpdateStatus::Stop();
    }
  }

  return {};
}

void Telemetry::Stop() {
  state_ = State::kStopped;
  Action::Trigger();
}

void Telemetry::SendTelemetry() {
  AE_TELE_DEBUG(TelemetrySending);

  state_ = State::kWaitRequest;
  auto server_num = request_for_server_.value_or(0);
  request_for_server_.reset();

  if (cloud_connection_->servers().size() <= server_num) {
    AE_TELED_ERROR("Requested server number is out of range");
    return;
  }

  ClientServerConnection* con =
      cloud_connection_->servers()[server_num]->client_connection();
  assert((con != nullptr) && "ClientServerConnection is null");

  auto telemetry = CollectTelemetry(con->stream_info());
  if (!telemetry) {
    AE_TELE_ERROR(TelemetryCollectFailed);
    return;
  }

  CloudRequest::CallApi(
      AuthApiCaller{[&](ApiContext<AuthorizedApi>& auth_api, auto*) {
        auth_api->send_telemetry(std::move(*telemetry));
      }},
      *cloud_connection_, RequestPolicy::Priority{server_num});

  AE_TELE_INFO(TelemetrySent);
}

void Telemetry::OnRequestTelemetry(std::size_t server_priority) {
  state_ = State::kSendTelemetry;
  request_for_server_ = server_priority;
  Action::Trigger();
}

std::optional<Telemetric> Telemetry::CollectTelemetry(
    StreamInfo const& stream_info) {
  auto aether_ptr = aether_.Lock();
  if (!aether_ptr) {
    assert(false);
    return std::nullopt;
  }
  auto& statistics_storage =
      aether_ptr->tele_statistics->trap()->statistics_store;
  auto& env_storage = statistics_storage.env_store();
  Telemetric res{};
  res.cpp.utm_id = env_storage.utm_id;
  res.cpp.lib_version = env_storage.library_version;
  res.cpp.os = env_storage.platform;
  res.cpp.compiler =
      Format("{}-{}", env_storage.compiler, env_storage.compiler_version);

  // max element size - telemetry struct size except blob
  auto blob_max_size = stream_info.max_element_size -
                       (11 + res.cpp.lib_version.size() + res.cpp.os.size() +
                        res.cpp.compiler.size());
  res.cpp.blob.reserve(blob_max_size);
  auto vector_writer =
      LimitVectorWriter<>{res.cpp.blob, res.cpp.blob.capacity()};
  auto os = omstream{vector_writer};
  os << statistics_storage;
  // TODO: should we reset stored statistics?

  return res;
}

}  // namespace ae

#endif
