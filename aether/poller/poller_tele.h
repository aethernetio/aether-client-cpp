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

#ifndef AETHER_POLLER_POLLER_TELE_H_
#define AETHER_POLLER_POLLER_TELE_H_

#include "aether/tele/tele.h"

AE_TELE_MODULE(kPoller, 6, 121, 155);

AE_TAG(kEpollWorkerCreate, kPoller)
AE_TAG(kEpollWorkerDestroyed, kPoller)
AE_TAG(kEpollAddDescriptor, kPoller)
AE_TAG(kEpollRemoveDescriptor, kPoller)
AE_TAG(kEpollAddFailed, kPoller)
AE_TAG(kEpollRemoveFailed, kPoller)
AE_TAG(kEpollCreateWakeUpFailed, kPoller)
AE_TAG(kEpollInitFailed, kPoller)
AE_TAG(kEpollWaitFailed, kPoller)

AE_TAG(kKqueueWorkerCreate, kPoller)
AE_TAG(kKqueueWorkerDestroyed, kPoller)
AE_TAG(kKqueueAddDescriptor, kPoller)
AE_TAG(kKqueueRemoveDescriptor, kPoller)
AE_TAG(kKqueueAddFailed, kPoller)
AE_TAG(kKqueueRemoveFailed, kPoller)
AE_TAG(kKqueueCreateWakeUpFailed, kPoller)
AE_TAG(kKqueueWakeUpFailed, kPoller)
AE_TAG(kKqueueInitFailed, kPoller)
AE_TAG(kKqueueWaitFailed, kPoller)
AE_TAG(kKqueueUnknownType, kPoller)

AE_TAG(kFreertosWorkerCreate, kPoller)
AE_TAG(kFreertosWorkerDestroyed, kPoller)
AE_TAG(kFreertosAddDescriptor, kPoller)
AE_TAG(kFreertosRemoveDescriptor, kPoller)
AE_TAG(kFreertosWaitFailed, kPoller)

AE_TAG(kWinpollWorkerCreate, kPoller)
AE_TAG(kWinpollWorkerDestroyed, kPoller)
AE_TAG(kWinpollAddDescriptor, kPoller)
AE_TAG(kWinpollRemoveDescriptor, kPoller)
AE_TAG(kWinpollAddFailed, kPoller)
AE_TAG(kWinpollInitFailed, kPoller)
AE_TAG(kWinpollWaitFailed, kPoller)

#endif  // AETHER_POLLER_POLLER_TELE_H_
