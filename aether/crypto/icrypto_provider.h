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

#ifndef AETHER_CRYPTO_ICRYPTO_PROVIDER_H_
#define AETHER_CRYPTO_ICRYPTO_PROVIDER_H_

#include "aether/types/data_buffer.h"

namespace ae {
class IEncryptProvider {
 public:
  virtual ~IEncryptProvider() = default;

  /**
   * \brief Encrypts the data.
   */
  virtual DataBuffer Encrypt(DataBuffer const& data) = 0;
  virtual std::size_t EncryptOverhead() const = 0;
};

class IDecryptProvider {
 public:
  virtual ~IDecryptProvider() = default;

  /**
   * \brief Decrypts the data.
   */
  virtual DataBuffer Decrypt(DataBuffer const& data) = 0;
};
}  // namespace ae

#endif  // AETHER_CRYPTO_ICRYPTO_PROVIDER_H_
