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

#  include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
Telemetry::Telemetry(ActionContext action_context, ObjPtr<Aether> const& aether,
                     ClientToServerStream& client_to_server)
    : Action{action_context},
      aether_{aether},
      client_to_server_{&client_to_server},
      telemetry_request_sub_{
          EventSubscriber{
              client_to_server_->client_safe_api().request_telemetric}
              .Subscribe(*this, MethodPtr<&Telemetry::OnRequestTelemetry>{})},
      state_{State::kWaitRequest} {
  AE_TELE_INFO(TelemetryCreated);
}

ActionResult Telemetry::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaitRequest:
        break;
      case State::kSendTelemetry:
        SendTelemetry();
        break;
    }
  }

  return {};
}

void Telemetry::SendTelemetry() {
  AE_TELE_DEBUG(TelemetrySending);

  state_ = State::kWaitRequest;
  auto telemetry = CollectTelemetry();
  if (!telemetry) {
    AE_TELE_ERROR(TelemetryCollectFailed);
    return;
  }
  auto auth_api = client_to_server_->authorized_api_adapter();
  auth_api->send_telemetric(std::move(*telemetry));
  auth_api.Flush();
  AE_TELE_INFO(TelemetrySent);
}

void Telemetry::OnRequestTelemetry() {
  state_ = State::kSendTelemetry;
  Action::Trigger();
}

std::optional<Telemetric> Telemetry::CollectTelemetry() {
  auto aether_ptr = aether_.Lock();
  if (!aether_ptr) {
    assert(false);
    return std::nullopt;
  }
  auto& statistics_storage =
      aether_ptr->tele_statistics()->trap()->statistics_store;
  auto& env_storage = statistics_storage.env_store();
  Telemetric res{};
  res.utm_id = env_storage.utm_id;
  res.cpp.lib_version = env_storage.library_version;
  res.cpp.os = env_storage.platform;
  res.cpp.compiler =
      Format("{}-{}", env_storage.compiler, env_storage.compiler_version);

  auto vector_writer = VectorWriter<>{res.blob};
  auto os = omstream{vector_writer};
  os << statistics_storage;
  // TODO: should we reset stored statistics?

  return res;
}

}  // namespace ae

#endif
