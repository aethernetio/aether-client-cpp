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

#include "aether/stream_api/crypto_gate.h"

#include <utility>

namespace ae {

CryptoGate::CryptoGate(std::unique_ptr<IEncryptProvider> crypto_encrypt,
                       std::unique_ptr<IDecryptProvider> crypto_decrypt)
    : crypto_encrypt_{std::move(crypto_encrypt)},
      crypto_decrypt_{std::move(crypto_decrypt)} {}

DataBuffer CryptoGate::WriteIn(DataBuffer buffer) {
  return crypto_encrypt_->Encrypt(std::move(buffer));
}

DataBuffer CryptoGate::WriteOut(DataBuffer buffer) {
  return crypto_decrypt_->Decrypt(std::move(buffer));
}

std::size_t CryptoGate::Overhead() const {
  return crypto_encrypt_->EncryptOverhead();
}
}  // namespace ae
