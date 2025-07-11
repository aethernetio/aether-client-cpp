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

#ifndef AETHER_DNS_DNS_TELE_H_
#define AETHER_DNS_DNS_TELE_H_

#include "aether/tele/tele.h"

AE_TELE_MODULE(kDns, 7, 156, 165);

AE_TAG(kAresDnsQueryHost, kDns)
AE_TAG(kAresDnsQueryError, kDns)
AE_TAG(kAresDnsQuerySuccess, kDns)
AE_TAG(kAresDnsFailedInitialize, kDns)
AE_TAG(kEspDnsQueryHost, kDns)
AE_TAG(kEspDnsQueryError, kDns)
AE_TAG(kEspDnsQuerySuccess, kDns)

#endif  // AETHER_DNS_DNS_TELE_H_
