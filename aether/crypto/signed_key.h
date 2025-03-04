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

#ifndef AETHER_CRYPTO_SIGNED_KEY_H_
#define AETHER_CRYPTO_SIGNED_KEY_H_

#include "aether/config.h"

#include "aether/crypto/key.h"
#include "aether/crypto/sign.h"

#include "aether/reflect/reflect.h"

namespace ae {
#if AE_SIGNATURE != AE_NONE
struct SignedKey {
  AE_REFLECT_MEMBERS(key, sign)

  Key key;
  Sign sign;
};

// Signature
bool CryptoSignVerify(Sign const& signature, Key const& pk, Key const& sign_pk);
#endif  // AE_SIGNATURE != AE_NONE
}  // namespace ae

#endif  // AETHER_CRYPTO_SIGNED_KEY_H_
