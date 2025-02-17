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

AE_TAG_INDEXED(TcpTransportConnect, ae::tele::Module::kTransport, 20)
AE_TAG(TcpTransport, ae::tele::Module::kTransport)
AE_TAG(TcpTransportDisconnect, ae::tele::Module::kTransport)
AE_TAG(TcpTransportSend, ae::tele::Module::kTransport)
AE_TAG(TcpTransportOnData, ae::tele::Module::kTransport)
AE_TAG(TcpTransportReceive, ae::tele::Module::kTransport)

#endif  // AETHER_TRANSPORT_TRANSPORT_TELE_H_
