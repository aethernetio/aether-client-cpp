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

#include "aether/work_cloud_api/work_server_api/authorized_api.h"

namespace ae {
AuthorizedApi::AuthorizedApi(ProtocolContext& protocol_context,
                             ActionContext action_context)
    : ApiClass{protocol_context},
      ping{protocol_context, action_context},
      send_message{protocol_context},
      send_messages{protocol_context},
      check_access_for_send_message{protocol_context, action_context},
      resolver_servers{protocol_context},
      resolver_clouds{protocol_context},
      send_telemetry{protocol_context} {}
}  // namespace ae
