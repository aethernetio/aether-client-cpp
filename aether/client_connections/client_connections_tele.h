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

#ifndef AETHER_CLIENT_CONNECTIONS_CLIENT_CONNECTIONS_TELE_H_
#define AETHER_CLIENT_CONNECTIONS_CLIENT_CONNECTIONS_TELE_H_

#include "aether/tele/tele.h"

AE_TELE_MODULE(kCloudClientConnection, 30);

AE_TAG_INDEXED(CloudClientConnStreamCreate, kCloudClientConnection, 110)
AE_TAG(CloudClientConnStreamClose, kCloudClientConnection)
AE_TAG(CloudClientNewStream, kCloudClientConnection)

AE_TELE_MODULE(kClientServerStream, 61);
AE_TAG(ClientServerStreamCreate, kClientServerStream)

AE_TELE_MODULE(kClientConnectionManager, 62);
AE_TAG(ClientConnectionManagerSelfCloudConnection, kClientConnectionManager)
AE_TAG(ClientConnectionManagerUidCloudConnection, kClientConnectionManager)
AE_TAG(ClientConnectionManagerUnableCreateClientServerConnection,
       kClientConnectionManager)

#endif  // AETHER_CLIENT_CONNECTIONS_CLIENT_CONNECTIONS_TELE_H_
