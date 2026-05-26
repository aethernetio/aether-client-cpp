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

#include "aether/crypto/signed_key.h"

#include "aether/crypto/crypto_definitions.h"

#if AE_SIGNATURE == AE_ED25519
#  include <sodium/crypto_sign.h>
#endif

#if AE_SIGNATURE == AE_HYDRO_SIGNATURE
#  include <hydrogen.h>
#endif

namespace ae {

#if AE_SIGNATURE != AE_NONE
bool CryptoSignVerify(Sign const& signature, Key const& pk,
                      Key const& sign_pk) {
#  if AE_SIGNATURE == AE_ED25519
  assert(signature.Index() == SignatureMethod::kEd25519);
  assert(pk.Index() == CryptoKeyType::kSodiumCurvePublic);
  assert(sign_pk.Index() == CryptoKeyType::kSodiumSignPublic);

  return crypto_sign_verify_detached(signature.Data(), pk.Data(), pk.Size(),
                                     sign_pk.Data()) == 0;
#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
  assert(signature.Index() == SignatureMethod::kHydroSignature);
  assert(pk.Index() == CryptoKeyType::kHydrogenCurvePublic);
  assert(sign_pk.Index() == CryptoKeyType::kHydrogenSignPublic);

  return hydro_sign_verify(signature.Data(), pk.Data(), pk.Size(),
                           HYDRO_CONTEXT, sign_pk.Data()) == 0;
#  endif  // AE_SIGNATURE == AE_ED25519
}
#endif
}  // namespace ae
