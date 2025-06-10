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
#ifndef EXAMPLES_C_API_AETHER_CAPI_H_
#define EXAMPLES_C_API_AETHER_CAPI_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AetherClassHandle AetherClassHandle;  // Opaque pointer

typedef struct CUid {
  uint8_t id[16];
} CUid;

AetherClassHandle* AetherClassCreate(const char* kWifiSsid, const char* kWifiPass, void (*pt2Func)(struct CUid uid, uint8_t const* data,
                                     size_t size, void* user_data));
int AetherClassDestroy(AetherClassHandle* handle);
void AetherClassInit(AetherClassHandle* handle);
void AetherClassSendMessages(AetherClassHandle* handle, char const* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif  // EXAMPLES_C_API_AETHER_CAPI_H_
