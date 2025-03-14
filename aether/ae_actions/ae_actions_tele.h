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

#ifndef AETHER_AE_ACTIONS_AE_ACTIONS_TELE_H_
#define AETHER_AE_ACTIONS_AE_ACTIONS_TELE_H_

#include "aether/tele/tele.h"

#if AE_SUPPORT_REGISTRATION
AE_TELE_MODULE(kRegister, 1000);
AE_TAG_INDEXED(RegisterStarted, kRegister, 1000)
AE_TAG(RegisterKeysGenerated, kRegister)
AE_TAG(RegisterServerConnectionListOver, kRegister)
AE_TAG(RegisterConnectionErrorTryNext, kRegister)
AE_TAG(RegisterGetKeys, kRegister)
AE_TAG(RegisterWaitKeysTimeout, kRegister)
AE_TAG(RegisterGetKeysResponse, kRegister)
AE_TAG(RegisterGetKeysVerificationFailed, kRegister)
AE_TAG(RegisterRequestPowParams, kRegister)
AE_TAG(RegisterPowParamsResponse, kRegister)
AE_TAG(RegisterPowParamsVerificationFailed, kRegister)
AE_TAG(RegisterMakeRegistration, kRegister)
AE_TAG(RegisterRegistrationConfirmed, kRegister)
AE_TAG(RegisterResolveCloud, kRegister)
AE_TAG(RegisterResolveCloudResponse, kRegister)
AE_TAG(RegisterClientRegistered, kRegister)
#endif

AE_TELE_MODULE(kAeActions, 3);
AE_TAG_INDEXED(kGetClientCloud, kAeActions, 50)
AE_TAG(kGetClientCloudRequestCloud, kAeActions)
AE_TAG(kGetClientCloudRequestServerResolve, kAeActions)
AE_TAG(kGetClientCloudServerResolveStopped, kAeActions)
AE_TAG(kGetClientCloudServerResolveFailed, kAeActions)
AE_TAG(kGetClientCloudServerResolved, kAeActions)

AE_TAG(kGetClientCloudConnection, kAeActions)
AE_TAG(kGetClientCloudConnectionDestroyed, kAeActions)
AE_TAG(kGetClientCloudConnectionCcmNull, kAeActions)
AE_TAG(kGetClientCloudConnectionCacheHit, kAeActions)
AE_TAG(kGetClientCloudConnectionServerListIsOver, kAeActions)

AE_TAG(kPing, kAeActions)
AE_TAG(kPingSend, kAeActions)
AE_TAG(kPingWriteError, kAeActions)

#endif  // AETHER_AE_ACTIONS_AE_ACTIONS_TELE_H_
