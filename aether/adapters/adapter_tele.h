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

AE_TELE_MODULE(kAdapter, 5);
AE_TAG_INDEXED(kAdapterCreate, kAdapter, 30)
AE_TAG(kAdapterDestructor, kAdapter)
AE_TAG(kAdapterCreateCacheMiss, kAdapter)
AE_TAG(kAdapterCreateCacheHit, kAdapter)
AE_TAG(kAdapterWifiTransportWait, kAdapter)
AE_TAG(kAdapterWifiTransportImmediately, kAdapter)
AE_TAG(kAdapterWifiConnected, kAdapter)
AE_TAG(kAdapterWifiDisconnected, kAdapter)
AE_TAG(kAdapterWifiInitiated, kAdapter)
AE_TAG(kAdapterWifiEventConnected, kAdapter)
AE_TAG(kAdapterWifiEventDisconnected, kAdapter)
AE_TAG(kAdapterWifiEventUnexpected, kAdapter)

#endif  // AETHER_ADAPTERS_ADAPTER_TELE_H_
