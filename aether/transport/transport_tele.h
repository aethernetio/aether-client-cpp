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

#ifndef AETHER_TRANSPORT_TRANSPORT_TELE_H_
#define AETHER_TRANSPORT_TRANSPORT_TELE_H_

#include "aether/tele/tele.h"

AE_TELE_MODULE(kTransport, 4, 51, 100);
AE_TELE_MODULE(kTransportDebug, 104, 400, 445);

AE_TAG(kUnixTcpTransportConnect, kTransport)
AE_TAG(kUnixTcpTransportDisconnect, kTransport)
AE_TAG(kUnixTcpTransport, kTransport)

AE_TAG(kUnixTcpTransportSend, kTransportDebug)
AE_TAG(kUnixTcpTransportReceive, kTransportDebug)

AE_TAG(kLwipTcpTransportConnect, kTransport)
AE_TAG(kLwipTcpTransportDisconnect, kTransport)
AE_TAG(kLwipTcpTransport, kTransport)

AE_TAG(kLwipTcpTransportSend, kTransportDebug)
AE_TAG(kLwipTcpTransportReceive, kTransportDebug)

AE_TAG(kWinTcpTransportConnect, kTransport)
AE_TAG(kWinTcpTransportDisconnect, kTransport)
AE_TAG(kWinTcpTransport, kTransport)

AE_TAG(kWinTcpTransportSend, kTransportDebug)
AE_TAG(kWinTcpTransportReceive, kTransportDebug)

#endif  // AETHER_TRANSPORT_TRANSPORT_TELE_H_
