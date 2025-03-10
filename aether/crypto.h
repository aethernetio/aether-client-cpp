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

#ifndef AETHER_CRYPTO_H_
#define AETHER_CRYPTO_H_

#include <map>

#include "aether/obj/obj.h"
#include "aether/crypto/sign.h"
#include "aether/crypto/key.h"

namespace ae {

class Crypto : public Obj {
  AE_OBJECT(Crypto, Obj, 0)

  Crypto() = default;

 public:
#ifdef AE_DISTILLATION
  Crypto(Domain* domain) : Obj{domain} {}
#endif  // AE_DISTILLATION

#if AE_SIGNATURE != AE_NONE
  AE_OBJECT_REFLECT(AE_MMBR(signs_pk_))
#else
  AE_OBJECT_REFLECT()
#endif

#if AE_SIGNATURE != AE_NONE
  std::map<SignatureMethod, Key> signs_pk_;
#endif
};

}  // namespace ae

#endif  // AETHER_CRYPTO_H_ */
