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

#ifndef AETHER_CLOUD_CONNECTIONS_CLOUD_CALLBACKS_H_
#define AETHER_CLOUD_CONNECTIONS_CLOUD_CALLBACKS_H_

#include "aether/types/small_function.h"

#include "aether/events/event_deleter.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/work_cloud_api/client_api/client_api_safe.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

namespace ae {
class CloudServerConnection;
class CloudServerConnections;
class CloudRequestAction;

// subscribe to client's api events
struct ClientListener : SmallFunction<EventHandlerDeleter(
                            ClientApiSafe& client_api,
                            CloudServerConnection* server_connection)> {};

// call authorized api
struct AuthApiCaller
    : SmallFunction<void(ApiContext<AuthorizedApi>& auth_api,
                         CloudServerConnection* server_connection)> {};
// make request to authorized api
struct AuthApiRequest
    : SmallFunction<void(ApiContext<AuthorizedApi>& auth_api,
                         CloudServerConnection* server_connection,
                         CloudRequestAction* request)> {};

// listen to client's api response
struct ClientResponseListener
    : SmallFunction<EventHandlerDeleter(
          ClientApiSafe& client_api, CloudServerConnection* server_connection,
          CloudRequestAction* request)> {};

}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_CLOUD_CALLBACKS_H_
