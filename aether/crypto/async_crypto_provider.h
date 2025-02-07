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

#ifndef AETHER_CRYPTO_ASYNC_CRYPTO_PROVIDER_H_
#define AETHER_CRYPTO_ASYNC_CRYPTO_PROVIDER_H_

#include "aether/ptr/ptr.h"

#include "aether/crypto/icrypto_provider.h"
#include "aether/crypto/ikey_provider.h"

namespace ae {
class AsyncEncryptProvider : public IEncryptProvider {
 public:
  explicit AsyncEncryptProvider(Ptr<IAsyncKeyProvider> key_provider);

  DataBuffer Encrypt(DataBuffer const& data) override;
  std::size_t EncryptOverhead() const override;

 private:
  Ptr<IEncryptProvider> impl_;
};

class AsyncDecryptProvider : public IDecryptProvider {
 public:
  explicit AsyncDecryptProvider(Ptr<IAsyncKeyProvider> key_provider);
  DataBuffer Decrypt(DataBuffer const& data) override;

 private:
  Ptr<IDecryptProvider> impl_;
};
}  // namespace ae

#endif  // AETHER_CRYPTO_ASYNC_CRYPTO_PROVIDER_H_
