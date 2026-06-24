/*
 * Copyright 2026 Aethernet Inc.
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

#include "aether/connection_manager/get_cloud_aether.h"

#include "aether/connection_manager/client_cloud_manager.h"

#include "aether/tele/tele.h"

namespace ae {
GetCloudFromAether::GetCloudFromAether(AeContext const& ae_context,
                                       ClientCloudManager& client_cloud_manager,
                                       CloudServerConnections& cloud_connection,
                                       Uid const& client_uid)
    : ae_context_{ae_context},
      client_uid_{client_uid},
      cloud_update_sub_{client_cloud_manager.cloud_update_event().Subscribe(
          MethodPtr<&GetCloudFromAether::CloudUpdate>{this})},
      cloud_request_{ae_context_, cloud_connection} {
  RequestCloud();
}

GetCloudFromAether::ResultEvent::Subscriber
GetCloudFromAether::result_event() noexcept {
  return EventSubscriber{result_event_};
}

void GetCloudFromAether::RequestCloud() {
  AE_TELED_DEBUG("RequestCloud");

  cloud_request_.CallApi(
      AuthApiCaller{[&](ApiContext<AuthorizedApi>& auth_api,
                        CloudServerConnection* server_connection) {
        AE_TELED_DEBUG("Send cloud request for uid:{} at server:{}",
                       client_uid_, server_connection->server()->server_id);

        auth_api->report_applied_config(std::vector{AppliedConfig{
            .subject_uid = client_uid_,
            .config_version = -1,  // -1 means request config
        }});
      }},
      RequestPolicy::All{});
  // response shall be received through CloudUpdate by matching with uid
}

void GetCloudFromAether::CloudUpdate(
    Uid const& uid, Result<Cloud::ptr const&, int> const& res) {
  if (uid != client_uid_) {
    return;
  }

  if (res) {
    result_event_.Emit(Ok{res.value()});
  } else {
    result_event_.Emit(Error{res.error()});
  }
}

}  // namespace ae
