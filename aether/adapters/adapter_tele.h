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

#ifndef AETHER_ADAPTERS_ADAPTER_TELE_H_
#define AETHER_ADAPTERS_ADAPTER_TELE_H_

#include "aether/tele/tele.h"

AE_TELE_MODULE(kAdapter, 5, 101, 120);
AE_TAG(kEthernetAdapterCreate, kAdapter)
AE_TAG(kEspWifiAdapterCreate, kAdapter)
AE_TAG(kEspWifiAdapterWifiTransportWait, kAdapter)
AE_TAG(kEspWifiAdapterWifiTransportImmediately, kAdapter)
AE_TAG(kEspWifiAdapterWifiDisconnected, kAdapter)
AE_TAG(kEspWifiAdapterWifiInitiated, kAdapter)
AE_TAG(kEspWifiAdapterWifiEventConnected, kAdapter)
AE_TAG(kEspWifiAdapterWifiEventDisconnected, kAdapter)
AE_TAG(kEspWifiAdapterWifiEventUnexpected, kAdapter)

AE_TAG(kAdapterModemAdapterCreate, kAdapter)
AE_TAG(kAdapterModemTransportWait, kAdapter)
AE_TAG(kAdapterModemTransportImmediately, kAdapter)
AE_TAG(kAdapterModemConnected, kAdapter)
AE_TAG(kAdapterModemDisconnected, kAdapter)
AE_TAG(kAdapterModemAtError, kAdapter)

#endif  // AETHER_ADAPTERS_ADAPTER_TELE_H_
