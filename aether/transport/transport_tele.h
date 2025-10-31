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

AE_TAG(kTcpTransportConnect, kTransport)
AE_TAG(kTcpTransportDisconnect, kTransport)
AE_TAG(kTcpTransport, kTransport)

AE_TAG(kTcpTransportSend, kTransportDebug)
AE_TAG(kTcpTransportReceive, kTransportDebug)

AE_TAG(kUdpTransport, kTransport)
AE_TAG(kUdpTransportConnect, kTransport)
AE_TAG(kUdpTransportConnectFailed, kTransport)
AE_TAG(kUdpTransportDisconnect, kTransport)

AE_TAG(kUdpTransportSend, kTransportDebug)
AE_TAG(kUdpTransportReceive, kTransportDebug)

AE_TAG(kModemTransportSend, kTransportDebug)
AE_TAG(kModemTransportReceive, kTransportDebug)
AE_TAG(kModemTransport, kTransport)

AE_TAG(kLoraModuleTransportSend, kTransportDebug)
AE_TAG(kLoraModuleTransportReceive, kTransportDebug)
AE_TAG(kLoraModuleTransport, kTransport)

#endif  // AETHER_TRANSPORT_TRANSPORT_TELE_H_
